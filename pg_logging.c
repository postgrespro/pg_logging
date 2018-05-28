/*
 * pg_logging.c
 *      PostgreSQL logging interface.
 *
 * Copyright (c) 2018, Postgres Professional
 */
#include "postgres.h"
#include "fmgr.h"
#include "miscadmin.h"
#include "storage/dsm.h"
#include "storage/shm_mq.h"
#include "storage/shm_toc.h"
#include "storage/ipc.h"
#include "string.h"
#include "utils/guc.h"

#include "pg_logging.h"

PG_MODULE_MAGIC;

void		_PG_init(void);
void		_PG_fini(void);

/* global variables */
int						buffer_size = 0;
shm_toc				   *toc = NULL;
LoggingShmemHdr		   *hdr = NULL;

static emit_log_hook_type		pg_logging_log_hook_next = NULL;
static shmem_startup_hook_type	pg_logging_shmem_hook_next = NULL;

#define safe_strlen(s) ((s) ? strlen(s) : 0)

static char *
add_block(char *data, char *block, int bytes_cp)
{
	uint32 endpos = data - hdr->data;

	Assert(bytes_cp < buffer_size);

	if (bytes_cp)
	{
		if (bytes_cp < buffer_size - endpos)
		{
			/* enough place to put */
			memcpy(data, block, bytes_cp);
			endpos += bytes_cp;
			if (endpos == buffer_size)
				endpos = 0;
		}
		else
		{
			/* should add by two parts */
			int size1 = buffer_size - endpos;
			int size2 = bytes_cp - size1;

			memcpy(data, block, size1);
			memcpy(hdr->data, (char *) block + size1, size2);
			endpos = size2;
		}
	}

	return hdr->data + endpos;
}

static void
copy_error_data_to_shmem(ErrorData *edata)
{
	int		hdrlen;
	char   *data;
	CollectedItem	item;
	uint32	endpos, curpos;

	/* calculate length */
	hdrlen = offsetof(CollectedItem, data);
	item.totallen = hdrlen;
	item.elevel = edata->elevel;
	item.saved_errno = edata->saved_errno;
	item.message_len = safe_strlen(edata->message);
	item.detail_len = safe_strlen(edata->message);
	item.hint_len = safe_strlen(edata->hint);
	item.totallen += (item.message_len + item.detail_len + item.hint_len);

	/*
	 * Find the place to put the block.
	 * First  check the header, it there is not enough place for the header
	 * move to the beginning.
	 * Then check the place with the data and calculate the position
	 * according to data length.
	 */
	do {
		curpos = pg_atomic_read_u32(&hdr->endpos);
		if (curpos + hdrlen > buffer_size)
			curpos = 0;

		endpos = curpos + item.totallen;
		if (endpos >= buffer_size)
			endpos = endpos - buffer_size;
	} while (pg_atomic_compare_exchange_u32(&hdr->endpos, &curpos, endpos));

	data = (char *) INTALIGN(hdr->data + curpos);
	Assert(data < (hdr->data + buffer_size));
	memcpy(data, &item, hdrlen);
	data += hdrlen;

	data = add_block(data, edata->message, item.message_len);
	data = add_block(data, edata->detail, item.detail_len);
	data = add_block(data, edata->hint, item.hint_len);
}

static void
pg_logging_log_hook(ErrorData *edata)
{
	if (pg_logging_log_hook_next)
		pg_logging_log_hook_next(edata);

	copy_error_data_to_shmem(edata);
}

static Size
pg_logging_shmem_size(int buffer_size)
{
	shm_toc_estimator	e;
	Size				size;

	Assert(buffer_size != 0);
	shm_toc_initialize_estimator(&e);
	shm_toc_estimate_chunk(&e, sizeof(LoggingShmemHdr));
	shm_toc_estimate_chunk(&e, buffer_size);
	shm_toc_estimate_keys(&e, 2);
	size = shm_toc_estimate(&e);

	return size;
}

static void
pg_logging_shmem_hook(void)
{
	bool	found;
	Size	segsize = pg_logging_shmem_size(buffer_size);
	void   *addr;

	addr = ShmemInitStruct("pg_logging", segsize, &found);
	if (!found)
	{
		int tranche_id = LWLockNewTrancheId();

		toc = shm_toc_create(PG_LOGGING_MAGIC, addr, segsize);

		hdr = shm_toc_allocate(toc, sizeof(LoggingShmemHdr));
		hdr->buffer_size = buffer_size;
		pg_atomic_init_u32(&hdr->endpos, 0);

		/* initialize buffer lwlock */
		LWLockRegisterTranche(tranche_id, "pg_logging tranche");
		LWLockInitialize(&hdr->hdr_lock, tranche_id);

		shm_toc_insert(toc, 0, hdr);
		hdr->data = shm_toc_allocate(toc, buffer_size);
		shm_toc_insert(toc, 1, hdr->data);
	}
	else
	{
		toc = shm_toc_attach(PG_LOGGING_MAGIC, addr);
#if PG_VERSION_NUM >= 100000
		hdr = shm_toc_lookup(toc, 0, false);
#else
		hdr = shm_toc_lookup(toc, 0);
#endif
	}

	if (pg_logging_shmem_hook_next)
		pg_logging_shmem_hook_next();

	elog(LOG, "pg_logging initialized");
}

static void
setup_gucs(void)
{
	DefineCustomIntVariable(
		"pg_logging.buffer_size",
		"Sets size of the ring buffer used to keep logs", NULL,
		&buffer_size,
		1024 /* 1MB */,
		100,
		512 * 1024,	/* 512MB should be enough for everyone */
		PGC_SUSET,
		GUC_UNIT_KB,
		NULL, NULL, NULL
	);
}

/*
 * Module load callback
 */
void
_PG_init(void)
{
	if (!process_shared_preload_libraries_in_progress)
		return;

	setup_gucs();

	/* install hooks */
	pg_logging_log_hook_next	= emit_log_hook;
	emit_log_hook				= pg_logging_log_hook;
	pg_logging_shmem_hook_next	= shmem_startup_hook;
	shmem_startup_hook			= pg_logging_shmem_hook;
}

/*
 * Module unload callback
 */
void
_PG_fini(void)
{
	/* Uninstall hooks. */
	emit_log_hook = pg_logging_log_hook_next;
	shmem_startup_hook = pg_logging_shmem_hook_next;
}
