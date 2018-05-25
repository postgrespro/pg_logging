#ifndef PG_LOGGING_H
#define PG_LOGGING_H

/* CollectedItem contains offsets in saved block */
typedef struct CollectedItem
{
	int			totallen;		/* size of this block */
	int			saved_errno;	/* errno at entry */

	/* text offsets in data block */
	int			message_len;
	int			detail_len;			/* detail error message */
	int			hint_len;			/* hint message */

	/* texts are contained here */
	char		data[FLEXIBLE_ARRAY_MEMBER];
} CollectedItem;

/*
 * LogItem is very similar to ItemId. Beginning of the buffer contains
 * an array with LogItem instances, but actual data will be added from the
 * end of buffer.
 */
typedef struct LogItem {
	int				offset;
	uint16			size;
	pg_atomic_flag	locked;
} LogItem;

typedef uint16 BlockHeader;

typedef struct LoggingShmemHdr
{
	char		   *data;
	int				endpos;
	int				buffer_size;	/* total size of buffer */
	LWLock			hdr_lock;
} LoggingShmemHdr;

#define	PG_LOGGING_MAGIC	0xAABBCCDD

#endif
