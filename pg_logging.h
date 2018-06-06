#ifndef PG_LOGGING_H
#define PG_LOGGING_H

#include "postgres.h"
#include "pg_config.h"
#include "storage/lwlock.h"

#define CHECK_DATA

typedef enum ItemObjectType {
	IOT_NONE,
	IOT_TABLE,
	IOT_COLUMN,
	IOT_DATATYPE,
	IOT_CONSTRAINT
} ItemObjectType;

/* CollectedItem contains offsets in saved block */
typedef struct CollectedItem
{
#ifdef CHECK_DATA
	int			magic;
#endif
	int			totallen;		/* size of this block */
	int			saved_errno;	/* errno at entry */
	int			sqlerrcode;		/* encoded ERRSTATE */
	char		elevel;			/* error level */

	/* text lengths in data block */
	int			message_len;
	int			detail_len;			/* errdetail */
	int			detail_log_len;
	int			hint_len;			/* errhint */
	int			context_len;
	int			domain_len;
	int			context_domain_len;
	int			object_name_len;	/* table, column, or datatype */
	ItemObjectType	object_type;

	/* location in the code */
	int			filename_len;
	int			lineno;
	int			funcname_len;

	/* texts are contained here */
	char		data[FLEXIBLE_ARRAY_MEMBER];
} CollectedItem;

#define ITEM_HDR_LEN (offsetof(CollectedItem, data))

typedef struct LoggingShmemHdr
{
	char			   *data;
	volatile uint32		readpos;
	volatile uint32		endpos;
	int					buffer_size;	/* total size of buffer */
	LWLock				hdr_lock;
	bool				wraparound;
} LoggingShmemHdr;

struct ErrorLevel {
	char   *text;
	int		code;
};

#define	PG_LOGGING_MAGIC	0xAABBCCDD
#define	PG_ITEM_MAGIC		0x06054AB5

// view
#define Natts_pg_logging_data		14
#define Anum_pg_logging_level		1	/* int */
#define Anum_pg_logging_errno		2	/* int */
#define Anum_pg_logging_errcode		3	/* int */
#define Anum_pg_logging_message		4
#define Anum_pg_logging_detail		5
#define Anum_pg_logging_detail_log	6
#define Anum_pg_logging_hint		7
#define Anum_pg_logging_context		8
#define Anum_pg_logging_context_domain		9
#define Anum_pg_logging_domain		10
#define Anum_pg_logging_filename	11
#define Anum_pg_logging_lineno		12	/* int */
#define Anum_pg_logging_funcname	13
#define Anum_pg_logging_position	14	/* int */

extern struct ErrorLevel errlevel_wordlist[];
extern LoggingShmemHdr	*hdr;

void reset_counters_in_shmem(void);
struct ErrorLevel *get_errlevel (register const char *str, register size_t len);

#endif
