drop function get_log(bool);
drop type log_item;

/* make sure this type is correlated with enum in pg_logging.h */
create type log_item as (
	log_time			timestamp with time zone,
	level				int,
	pid					int,
	line_num			bigint,						/* log line number */
	appname				text,
	start_time			timestamp with time zone,	/* backend start time */
	datid				oid,						/* database id */
	errno				int,
	errcode				int,
	errstate			text,
	message				text,
	detail				text,
	detail_log			text,
	hint				text,
	context				text,
	context_domain		text,
	domain				text,
	internalpos			int,
	internalquery		text,
	userid				oid,
	remote_host			text,
	command_tag			text,
	vxid				text,						/* virtual transaction id */
	txid				bigint,						/* transaction id */
	query				text,
	query_pos			int,
	position			int							/* position in logs buffer */
);

create function get_log(
	flush			bool default true,
)
returns log_item as 'MODULE_PATHNAME', 'get_logged_data'
language c;

create function get_log(
	from_position	int
)
returns log_item as 'MODULE_PATHNAME', 'get_logged_data_from'
language c;
