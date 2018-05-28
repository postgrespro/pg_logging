/*
 * pg_logging.c
 *      PostgreSQL logging interface.
 *
 * Copyright (c) 2018, Postgres Professional
 */
#include "postgres.h"
#include "catalog/pg_type_d.h"
#include "funcapi.h"

#include "pg_logging.h"

PG_FUNCTION_INFO_V1( get_logged_data );
PG_FUNCTION_INFO_V1( errlevel_in );
PG_FUNCTION_INFO_V1( errlevel_out );
PG_FUNCTION_INFO_V1( errlevel_eq );

struct ErrorLevel *get_errlevel (register const char *str, register size_t len);
extern struct ErrorLevel errlevel_wordlist[];
extern LoggingShmemHdr	*hdr;

typedef struct {
	uint32		until;
} logged_data_ctx;

Datum
get_logged_data(PG_FUNCTION_ARGS)
{
	MemoryContext		old_mcxt;
	FuncCallContext	   *funccxt;
	logged_data_ctx	   *usercxt;

	/* reader will block readers */
	LWLockAcquire(&hdr->hdr_lock, LW_EXCLUSIVE);

	if (SRF_IS_FIRSTCALL())
	{
		TupleDesc	tupdesc;

		funccxt = SRF_FIRSTCALL_INIT();

		old_mcxt = MemoryContextSwitchTo(funccxt->multi_call_memory_ctx);

		usercxt = (logged_data_ctx *) palloc(sizeof(logged_data_ctx));
		usercxt->until = pg_atomic_read_u32(&hdr->endpos);

		/* Create tuple descriptor */
		tupdesc = CreateTemplateTupleDesc(4, false);

		TupleDescInitEntry(tupdesc, 1,
						   "level", INT4OID, -1, 0);
		TupleDescInitEntry(tupdesc, 2,
						   "errno", INT4OID, -1, 0);
		TupleDescInitEntry(tupdesc, 3,
						   "message", TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, 4,
						   "detail", TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, 4,
						   "hint", TEXTOID, -1, 0);

		funccxt->tuple_desc = BlessTupleDesc(tupdesc);
		funccxt->user_fctx = (void *) usercxt;

		MemoryContextSwitchTo(old_mcxt);
	}

	funccxt = SRF_PERCALL_SETUP();
	usercxt = (logged_data_ctx *) funccxt->user_fctx;

	pg_read_barrier();

	if (hdr->readpos < usercxt->until)
	{
		CollectedItem	*item;
		char			*data;

		data = hdr->data + INTALIGN(hdr->readpos);
		item = (CollectedItem *) data;
		hdr->readpos += item->totallen;
	}
	LWLockRelease(&hdr->hdr_lock);
}

Datum
errlevel_out(PG_FUNCTION_ARGS)
{
	int		i;
	int		code = PG_GETARG_INT32(0);

	for (i = 0; i < sizeof(13); i++)
	{
		struct ErrorLevel	*el = &errlevel_wordlist[i];
		if (el->code == code)
			PG_RETURN_CSTRING(pstrdup(el->text));
	}

	elog(ERROR, "Invalid error level name");
}

Datum
errlevel_in(PG_FUNCTION_ARGS)
{
	char   *str = PG_GETARG_CSTRING(0);
	int		len = strlen(str);
	struct ErrorLevel *el;

	if (len == 0)
		elog(ERROR, "Empty status name");

	el = get_errlevel(str, len);
	if (!el)
		elog(ERROR, "Unknown level name: %s", str);

	PG_RETURN_INT32(el->code);
}
