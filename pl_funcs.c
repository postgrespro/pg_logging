/*
 * pg_logging.c
 *      PostgreSQL logging interface.
 *
 * Copyright (c) 2018, Postgres Professional
 */
#include "postgres.h"
#include "catalog/pg_type_d.h"
#include "funcapi.h"
#include "utils/builtins.h"
#include "access/htup_details.h"

#include "pg_logging.h"

PG_FUNCTION_INFO_V1( get_logged_data );
PG_FUNCTION_INFO_V1( errlevel_in );
PG_FUNCTION_INFO_V1( errlevel_out );
PG_FUNCTION_INFO_V1( errlevel_eq );

typedef struct {
	uint32		until;
	uint32		readpos;
	bool		wraparound;
} logged_data_ctx;

static char *
get_errlevel_name(int code)
{
	int i;
	for (i = 0; i <= 21 /* MAX_HASH_VALUE */; i++)
	{
		struct ErrorLevel	*el = &errlevel_wordlist[i];
		if (el->text != NULL && el->code == code)
			return el->text;
	}
	elog(ERROR, "Invalid error level name");
}

Datum
get_logged_data(PG_FUNCTION_ARGS)
{
	MemoryContext		old_mcxt;
	FuncCallContext	   *funccxt;
	logged_data_ctx	   *usercxt;

	if (SRF_IS_FIRSTCALL())
	{
		TupleDesc	tupdesc;

		funccxt = SRF_FIRSTCALL_INIT();

		old_mcxt = MemoryContextSwitchTo(funccxt->multi_call_memory_ctx);

		/* reader will block other readers */
		LWLockAcquire(&hdr->hdr_lock, LW_EXCLUSIVE);
		usercxt = (logged_data_ctx *) palloc(sizeof(logged_data_ctx));
		usercxt->until = pg_atomic_read_u32(&hdr->endpos);
		usercxt->readpos = hdr->readpos;
		usercxt->wraparound = usercxt->until < hdr->readpos;

		/* Create tuple descriptor */
		tupdesc = CreateTemplateTupleDesc(Natts_pg_logging_data, false);

		TupleDescInitEntry(tupdesc, Anum_pg_logging_level,
						   "level", INT4OID, -1, 0);
		TupleDescInitEntry(tupdesc, Anum_pg_logging_errno,
						   "errno", INT4OID, -1, 0);
		TupleDescInitEntry(tupdesc, Anum_pg_logging_message,
						   "message", TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, Anum_pg_logging_detail,
						   "detail", TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, Anum_pg_logging_hint,
						   "hint", TEXTOID, -1, 0);

		funccxt->tuple_desc = BlessTupleDesc(tupdesc);
		funccxt->user_fctx = (void *) usercxt;

		MemoryContextSwitchTo(old_mcxt);
	}

	funccxt = SRF_PERCALL_SETUP();
	usercxt = (logged_data_ctx *) funccxt->user_fctx;

	pg_read_barrier();

	if ((!usercxt->wraparound && hdr->readpos < usercxt->until) ||
			(usercxt->wraparound && hdr->readpos > usercxt->until))
	{
		CollectedItem  *item;
		char		   *data;
		HeapTuple		htup;
		Datum					values[Natts_pg_logging_data];
		bool					isnull[Natts_pg_logging_data];

		MemSet(values, 0, sizeof(values));
		MemSet(isnull, 0, sizeof(isnull));

		data = (char *) INTALIGN(hdr->data + hdr->readpos);
		item = (CollectedItem *) data;
		hdr->readpos += item->totallen;
		if (item->totallen == 0 || hdr->readpos >= hdr->buffer_size)
		{
			usercxt->wraparound = false;
			hdr->readpos = 0;
		}

		values[Anum_pg_logging_level - 1] = Int32GetDatum(item->elevel);
		values[Anum_pg_logging_errno - 1] = Int32GetDatum(item->saved_errno);

		data = item->data;
		values[Anum_pg_logging_message - 1]	= CStringGetTextDatum(pstrdup(data));
		data += item->message_len;
		if (item->detail_len)
		{
			values[Anum_pg_logging_detail - 1]	=
					CStringGetTextDatum(pstrdup(data));
			data += item->detail_len;
		}
		else isnull[Anum_pg_logging_detail - 1] = true;

		if (item->hint_len)
		{
			values[Anum_pg_logging_hint - 1]	=
					CStringGetTextDatum(pstrdup(data));
			data += item->hint_len;
		}
		else isnull[Anum_pg_logging_hint - 1] = true;

		/* Form output tuple */
		htup = heap_form_tuple(funccxt->tuple_desc, values, isnull);

		SRF_RETURN_NEXT(funccxt, HeapTupleGetDatum(htup));
	}

	LWLockRelease(&hdr->hdr_lock);
	SRF_RETURN_DONE(funccxt);
}

Datum
errlevel_out(PG_FUNCTION_ARGS)
{
	PG_RETURN_CSTRING(get_errlevel_name(PG_GETARG_INT32(0)));
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
