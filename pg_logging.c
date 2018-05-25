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
	Assert(bytes_cp < buffer_size);

	if (bytes_cp)
	{
		if (bytes_cp < buffer_size - hdr->endpos)
		{
			memcpy(data + hdr->endpos, block, bytes_cp);
			hdr->endpos += bytes_cp;
			if (hdr->endpos == buffer_size)
				hdr->endpos = 0;
		}
		else
		{
			int size1 = buffer_size - hdr->endpos;
			int size2 = bytes_cp - size1;

			memcpy(data + hdr->endpos, block, size1);
			memcpy(data, (char *) block + size1, size2);
			hdr->endpos = size2;
		}
	}

	return hdr->data + hdr->endpos;
}

static void
copy_error_data_to_shmem(ErrorData *edata)
{
	int		hdrlen;
	char   *data;
	CollectedItem	*item;

	LWLockAcquire(&hdr->hdr_lock, LW_EXCLUSIVE);

	hdrlen = offsetof(CollectedItem, data);
	data = hdr->data + hdr->endpos;
	item = (CollectedItem *) data;
	data = data + offsetof(CollectedItem, data);

	item->saved_errno = edata->saved_errno;
	item->message_len = safe_strlen(edata->message);
	item->detail_len = safe_strlen(edata->message);
	item->hint_len = safe_strlen(edata->hint);

	/* copy blocks of data */
	item->totallen = hdrlen + item->message_len +
		item->detail_len + item->hint_len;
	data = add_block(data, edata->message, item->message_len);
	data = add_block(data, edata->detail, item->detail_len);
	data = add_block(data, edata->hint, item->hint_len);

	/* if there is not enough place for new header, to just move to start */
	if (data + hdrlen >= hdr->data + buffer_size)
	{
		item->totallen += (hdr->data + buffer_size) - data;
		hdr->endpos = 0;
	}

	LWLockRelease(&hdr->hdr_lock);
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
		hdr->endpos = 0;

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
