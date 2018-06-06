/*
 * pg_logging.c
 *      PostgreSQL logging interface.
 *
 * Copyright (c) 2018, Postgres Professional
 */
#include "postgres.h"
#include "tsearch/ts_locale.h"
#include "catalog/pg_type_d.h"
#include "funcapi.h"
#include "utils/builtins.h"
#include "access/htup_details.h"

#include "pg_logging.h"

PG_FUNCTION_INFO_V1( get_logged_data );
PG_FUNCTION_INFO_V1( flush_logged_data );
PG_FUNCTION_INFO_V1( test_ereport );
PG_FUNCTION_INFO_V1( errlevel_in );
PG_FUNCTION_INFO_V1( errlevel_out );
PG_FUNCTION_INFO_V1( errlevel_eq );

typedef struct {
	uint32		until;
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

void
reset_counters_in_shmem(void)
{
	LWLockAcquire(&hdr->hdr_lock, LW_EXCLUSIVE);
	hdr->endpos = 0;
	hdr->readpos = 0;
	hdr->wraparound = false;
	LWLockRelease(&hdr->hdr_lock);
}

Datum
flush_logged_data(PG_FUNCTION_ARGS)
{
	reset_counters_in_shmem();
	PG_RETURN_VOID();
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

		/*
		 * Reader will block only other readers if it's fast enough.
		 * Writer could take this lock if readpos wasn't changed.
		 */
		LWLockAcquire(&hdr->hdr_lock, LW_EXCLUSIVE);
		usercxt = (logged_data_ctx *) palloc(sizeof(logged_data_ctx));
		usercxt->until = hdr->endpos;
		usercxt->wraparound = hdr->wraparound;
		hdr->wraparound = false;

		/* Create tuple descriptor */
		tupdesc = CreateTemplateTupleDesc(Natts_pg_logging_data, false);

		TupleDescInitEntry(tupdesc, Anum_pg_logging_level,
						   "level", INT4OID, -1, 0);
		TupleDescInitEntry(tupdesc, Anum_pg_logging_errno,
						   "errno", INT4OID, -1, 0);
		TupleDescInitEntry(tupdesc, Anum_pg_logging_errcode,
						   "errcode", INT4OID, -1, 0);
		TupleDescInitEntry(tupdesc, Anum_pg_logging_message,
						   "message", TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, Anum_pg_logging_detail,
						   "detail", TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, Anum_pg_logging_detail_log,
						   "detail_log", TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, Anum_pg_logging_hint,
						   "hint", TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, Anum_pg_logging_context,
						   "context", TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, Anum_pg_logging_context_domain,
						   "context_domain", TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, Anum_pg_logging_domain,
						   "domain", TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, Anum_pg_logging_filename,
						   "filename", TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, Anum_pg_logging_lineno,
						   "lineno", INT4OID, -1, 0);
		TupleDescInitEntry(tupdesc, Anum_pg_logging_funcname,
						   "funcname", TEXTOID, -1, 0);
		TupleDescInitEntry(tupdesc, Anum_pg_logging_position,
						   "position", INT4OID, -1, 0);

		funccxt->tuple_desc = BlessTupleDesc(tupdesc);
		funccxt->user_fctx = (void *) usercxt;

		MemoryContextSwitchTo(old_mcxt);
	}

	funccxt = SRF_PERCALL_SETUP();
	usercxt = (logged_data_ctx *) funccxt->user_fctx;

	pg_read_barrier();

	while ((!usercxt->wraparound && hdr->readpos < usercxt->until) ||
			(usercxt->wraparound && hdr->readpos > usercxt->until))
	{
		CollectedItem  *item;
		char		   *data;
		HeapTuple		htup;
		Datum			values[Natts_pg_logging_data];
		bool			isnull[Natts_pg_logging_data];
		int				curpos;

		if (hdr->readpos + ITEM_HDR_LEN > hdr->buffer_size)
		{
			hdr->readpos = 0;
			usercxt->wraparound = false;
			continue;
		}

		curpos = hdr->readpos;
		data = (char *) (hdr->data + hdr->readpos);
		AssertPointerAlignment(data, 4);

		/*
		 * careful here, we point to the buffer first, then allocate a
		 * block using information from buffer
		 */
		item = (CollectedItem *) data;
		Assert(item->totallen < hdr->buffer_size);

		item = (CollectedItem *) palloc0(item->totallen);
		memcpy(item, data, offsetof(CollectedItem, data));
		data += ITEM_HDR_LEN;

		if (hdr->readpos + item->totallen >= hdr->buffer_size)
		{
			/* two parts */
			int	taillen = hdr->buffer_size - hdr->readpos - ITEM_HDR_LEN;
			memcpy(item->data, data, taillen);
			usercxt->wraparound = false;
			hdr->readpos += item->totallen;
			hdr->readpos = hdr->readpos - hdr->buffer_size;
			memcpy(item->data + taillen, hdr->data, hdr->readpos);
		}
		else
		{
			/* one part */
			memcpy(item->data, data, item->totallen - ITEM_HDR_LEN);
			hdr->readpos += item->totallen;
		}

		MemSet(values, 0, sizeof(values));
		MemSet(isnull, 0, sizeof(isnull));

		values[Anum_pg_logging_level - 1] = Int32GetDatum(item->elevel);
		values[Anum_pg_logging_errno - 1] = Int32GetDatum(item->saved_errno);
		values[Anum_pg_logging_errcode - 1] = Int32GetDatum(item->sqlerrcode);
		values[Anum_pg_logging_lineno - 1] = Int32GetDatum(item->lineno);
		values[Anum_pg_logging_position - 1] = Int32GetDatum(curpos);

		data = item->data;
#define	EXTRACT_VAL_TO(attnum, len)								\
do {															\
	if (len) {													\
		values[(attnum) - 1]	= CStringGetTextDatum(data);	\
		data += (len);											\
	}															\
	else isnull[(attnum) - 1] = true;							\
} while (0);

		/* ordering is important !! */
		EXTRACT_VAL_TO(Anum_pg_logging_message, item->message_len);
		EXTRACT_VAL_TO(Anum_pg_logging_detail, item->detail_len);
		EXTRACT_VAL_TO(Anum_pg_logging_detail_log, item->detail_log_len);
		EXTRACT_VAL_TO(Anum_pg_logging_hint, item->hint_len);
		EXTRACT_VAL_TO(Anum_pg_logging_context, item->context_len);
		EXTRACT_VAL_TO(Anum_pg_logging_filename, item->filename_len);
		EXTRACT_VAL_TO(Anum_pg_logging_funcname, item->funcname_len);
		EXTRACT_VAL_TO(Anum_pg_logging_domain, item->domain_len);
		EXTRACT_VAL_TO(Anum_pg_logging_context_domain, item->context_domain_len);

		/* Form output tuple */
		htup = heap_form_tuple(funccxt->tuple_desc, values, isnull);
		pfree(item);

		SRF_RETURN_NEXT(funccxt, HeapTupleGetDatum(htup));
	}

	LWLockRelease(&hdr->hdr_lock);
	SRF_RETURN_DONE(funccxt);
}

Datum
test_ereport(PG_FUNCTION_ARGS)
{
	ereport(PG_GETARG_INT32(0),
			(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
			 errmsg("%s", PG_GETARG_CSTRING(1)),
			 errdetail("%s", PG_GETARG_CSTRING(2)),
			 errhint("%s", PG_GETARG_CSTRING(3))));
	PG_RETURN_VOID();
}

Datum
errlevel_out(PG_FUNCTION_ARGS)
{
	PG_RETURN_CSTRING(get_errlevel_name(PG_GETARG_INT32(0)));
}

Datum
errlevel_in(PG_FUNCTION_ARGS)
{
	char   *str = lowerstr(PG_GETARG_CSTRING(0));
	int		len = strlen(str);
	struct ErrorLevel *el;

	if (len == 0)
		elog(ERROR, "Empty status name");

	el = get_errlevel(str, len);
	if (!el)
		elog(ERROR, "Unknown level name: %s", str);

	PG_RETURN_INT32(el->code);
}
