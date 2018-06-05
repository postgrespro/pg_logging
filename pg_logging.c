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
#include "utils/builtins.h"

#include "pg_logging.h"

PG_MODULE_MAGIC;

void		_PG_init(void);
void		_PG_fini(void);

/* global variables */
int						buffer_size_setting = 0;
int						buffer_position = 0;	/* just mock */
shm_toc				   *toc = NULL;
LoggingShmemHdr		   *hdr = NULL;
bool					shmem_initialized = false;
bool					buffer_increase_suggested = false;
bool					log_in_process = false;

static emit_log_hook_type		pg_logging_log_hook_next = NULL;
static shmem_startup_hook_type	pg_logging_shmem_hook_next = NULL;

static void pg_logging_log_hook(ErrorData *edata);
static void	pg_logging_shmem_hook(void);

enum {
	WRAP_NONE,
	WRAP_PARTIAL,
	WRAP_FULL,
};

#define safe_strlen(s) ((s) ? strlen(s) + 1 : 0)

static void
buffer_size_assign_hook(int newval, void *extra)
{
	if (!shmem_initialized)
		return;

	newval = newval * 1024;
	if (newval > hdr->buffer_size)
		elog(ERROR, "buffer size cannot be increased");

	reset_counters_in_shmem();
	hdr->buffer_size = newval;
}

static bool
buffer_position_check_hook(int *newval, void **extra, GucSource source)
{
	if (shmem_initialized)
		elog(ERROR, "this parameter could not be changed");
	return true;
}

static const char *
buffer_position_show_hook(void)
{
	return psprintf("endpos: %d, readpos: %d", hdr->endpos, hdr->readpos);
}

static void
setup_gucs(void)
{
	DefineCustomIntVariable(
		"pg_logging.buffer_size",
		"Sets size of the ring buffer used to keep logs", NULL,
		&buffer_size_setting,
		1024, //1024 /* 1MB */,
		1,
		512 * 1024,	/* 512MB should be enough for everyone */
		PGC_SUSET,
		GUC_UNIT_KB,
		NULL, buffer_size_assign_hook, NULL
	);

	DefineCustomIntVariable(
		"pg_logging.buffer_position",
		"Used to check current position in the buffer", NULL,
		&buffer_position,
		0,
		0,
		INT_MAX,	/* 512MB should be enough for everyone */
		PGC_SUSET,
		GUC_UNIT_BYTE,
		buffer_position_check_hook, NULL, buffer_position_show_hook
	);
}

static void
install_hooks(void)
{
	pg_logging_log_hook_next	= emit_log_hook;
	emit_log_hook				= pg_logging_log_hook;
	pg_logging_shmem_hook_next	= shmem_startup_hook;
	shmem_startup_hook			= pg_logging_shmem_hook;
}

static void
uninstall_hooks(void)
{
	emit_log_hook		= pg_logging_log_hook_next;
	shmem_startup_hook	= pg_logging_shmem_hook_next;
}

static char *
add_block(char *data, char *block, int bytes_cp)
{
	uint32 endpos = data - hdr->data;

	Assert(bytes_cp < hdr->buffer_size);

	if (bytes_cp)
	{
		if (bytes_cp < hdr->buffer_size - endpos)
		{
			/* enough place to put */
			memcpy(data, block, bytes_cp);
			endpos += bytes_cp;
			if (endpos == hdr->buffer_size)
				endpos = 0;
		}
		else
		{
			/* should add by two parts */
			int size1 = hdr->buffer_size - endpos;
			int size2 = bytes_cp - size1;

			memcpy(data, block, size1);
			memcpy(hdr->data, (char *) block + size1, size2);
			endpos = size2;
		}
	}

	return hdr->data + endpos;
}

static int
push_reading_position(int readpos, bool *wrapped)
{
	CollectedItem *item;

	*wrapped = false;
	if (readpos + ITEM_HDR_LEN > hdr->buffer_size)
	{
		readpos = 0;
		*wrapped = true;

#ifdef CHECK_DATA
		item = (CollectedItem *) hdr->data;
		Assert(item->magic == PG_ITEM_MAGIC);
#endif

	}
	else
	{
		int tail;

		item = (CollectedItem *) (hdr->data + readpos);
#ifdef CHECK_DATA
		Assert(item->magic == PG_ITEM_MAGIC);
#endif
		readpos += item->totallen;
		tail = readpos - hdr->buffer_size;
		if (tail > 0) {
			readpos = tail;
			*wrapped = true;
		}
	}
	if (!buffer_increase_suggested)
	{
		fprintf(stderr, "CONSIDER INCREASING PG_LOGGING BUFFER, READPOS MOVED TO: %d\n",
				readpos);
		buffer_increase_suggested = true;
	}

	return readpos;
}

static void
copy_error_data_to_shmem(ErrorData *edata)
{
	char		   *data;
	int				wrap = WRAP_NONE;
	CollectedItem	item;
	uint32			endpos,
					curpos,
					savedpos;

	/* don't allow recursive logs */
	if (log_in_process)
		return;

	log_in_process = true;

	/* calculate length */
#ifdef CHECK_DATA
	item.magic = PG_ITEM_MAGIC;
#endif
	item.totallen = ITEM_HDR_LEN;
	item.elevel = edata->elevel;
	item.saved_errno = edata->saved_errno;
	item.message_len = safe_strlen(edata->message);
	item.detail_len = safe_strlen(edata->detail);
	item.hint_len = safe_strlen(edata->hint);
	item.totallen += INTALIGN(item.message_len + item.detail_len + item.hint_len);

	/*
	 * Find the place to put the block.
	 * First  check the header, it there is not enough place for the header
	 * move to the beginning.
	 * Then check the place with the data and calculate the position
	 * according to data length.
	 */

	LWLockAcquire(&hdr->hdr_lock, LW_EXCLUSIVE);
	savedpos = curpos = hdr->endpos;

	if (savedpos + ITEM_HDR_LEN > hdr->buffer_size)
	{
		savedpos = 0;
		wrap = WRAP_FULL;
	}

	endpos = savedpos + item.totallen;
	if (endpos >= hdr->buffer_size)
	{
		endpos = endpos - hdr->buffer_size;
		wrap = WRAP_PARTIAL;
	}
	hdr->endpos = endpos;

	if (wrap)
	{
		bool rwrap = false;

		hdr->wraparound = true;
		if (wrap == WRAP_PARTIAL && savedpos < hdr->readpos)
		{
			while (!rwrap)
				hdr->readpos = push_reading_position(hdr->readpos, &rwrap);
		}

		rwrap = false;
		while (hdr->readpos < endpos)
			hdr->readpos = push_reading_position(hdr->readpos, &rwrap);
		Assert(!rwrap);
	}
	else if (hdr->wraparound)
	{
		bool rwrap = false;

		while (hdr->readpos < endpos)
		{
			hdr->readpos = push_reading_position(hdr->readpos, &rwrap);
			if (rwrap)
			{
				hdr->wraparound = false;
				break;
			}
		}
	}

	data = hdr->data + savedpos;
	fprintf(stderr, "readpos: %d, curpos: %d, endpos %d\n",
		hdr->readpos, savedpos, hdr->endpos);
	Assert(data < (hdr->data + hdr->buffer_size));
	memcpy(data, &item, ITEM_HDR_LEN);
	data += ITEM_HDR_LEN;

	data = add_block(data, edata->message, item.message_len);
	data = add_block(data, edata->detail, item.detail_len);
	data = add_block(data, edata->hint, item.hint_len);

	LWLockRelease(&hdr->hdr_lock);

	log_in_process = false;
}

static void
pg_logging_log_hook(ErrorData *edata)
{
	if (pg_logging_log_hook_next)
		pg_logging_log_hook_next(edata);

	if (shmem_initialized && MyProc != NULL && !proc_exit_inprogress)
		copy_error_data_to_shmem(edata);
}

static Size
pg_logging_shmem_size(int bufsize)
{
	shm_toc_estimator	e;
	Size				size;

	Assert(bufsize != 0);
	shm_toc_initialize_estimator(&e);
	shm_toc_estimate_chunk(&e, sizeof(LoggingShmemHdr));
	shm_toc_estimate_chunk(&e, bufsize);
	shm_toc_estimate_keys(&e, 2);
	size = shm_toc_estimate(&e);

	return size;
}

static void
pg_logging_shmem_hook(void)
{
	bool	found;
	Size	bufsize = INTALIGN(buffer_size_setting * 1024);
	Size	segsize = pg_logging_shmem_size(bufsize);
	void   *addr;

	addr = ShmemInitStruct("pg_logging", segsize, &found);
	if (!found)
	{
		int tranche_id = LWLockNewTrancheId();

		toc = shm_toc_create(PG_LOGGING_MAGIC, addr, segsize);

		hdr = shm_toc_allocate(toc, sizeof(LoggingShmemHdr));
		hdr->buffer_size = bufsize;
		hdr->endpos = 0;
		hdr->readpos = 0;

		/* initialize buffer lwlock */
		LWLockRegisterTranche(tranche_id, "pg_logging tranche");
		LWLockInitialize(&hdr->hdr_lock, tranche_id);

		shm_toc_insert(toc, 0, hdr);
		hdr->data = shm_toc_allocate(toc, hdr->buffer_size);
		fprintf(stderr, "data addr: %p\n", hdr->data);
		shm_toc_insert(toc, 1, hdr->data);
	}
	else
	{
		toc = shm_toc_attach(PG_LOGGING_MAGIC, addr);
#if PG_VERSION_NUM >= 100000
		hdr = shm_toc_lookup(toc, 0, false);
#else
		hdr = shm_toc_lookup(toc, 0);
		fprintf(stderr, "data addr: %p\n", hdr->data);
#endif
	}

	shmem_initialized = true;

	if (pg_logging_shmem_hook_next)
		pg_logging_shmem_hook_next();

	elog(LOG, "pg_logging initialized");
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
	install_hooks();
}

/*
 * Module unload callback
 */
void
_PG_fini(void)
{
	uninstall_hooks();
}
