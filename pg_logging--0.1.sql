/* create special type for error levels */
create type error_level;

create or replace function errlevel_out(error_level)
returns cstring as 'MODULE_PATHNAME'
language c strict immutable parallel safe;

create or replace function errlevel_in(cstring)
returns error_level as 'MODULE_PATHNAME'
language c strict immutable parallel safe;

create type error_level (
	input = errlevel_in,
	output = errlevel_out,
	internallength = 4,
    passedbyvalue,
    alignment = int
);

create cast (int AS error_level) without function as assignment;

/* make sure this type is correlated with enum in pg_logging.h */
create type log_item as (
	log_time			timestamp with time zone,
	level				int,
	pid					int,
	line_num			bigint,
	appname				text,
	start_time			timestamp with time zone,
	datid				Oid,
	errno				int,
	errcode				int,
	message				text,
	detail				text,
	detail_log			text,
	hint				text,
	context				text,
	context_domain		text,
	domain				text,
	internalpos			int,
	internalquery		text,
	userid				Oid,
	remote_host			text,
	command_tag			text,
	vxid				text,
	txid				bigint
);

create or replace function get_log(
	flush	bool	/* always true for now */
)
returns log_item as 'MODULE_PATHNAME', 'get_logged_data'
language c;

create or replace function flush_log()
returns void as 'MODULE_PATHNAME', 'flush_logged_data'
language c;

create or replace function test_ereport(
	elevel		error_level,
	message		cstring,
	detail		cstring,
	hint		cstring
)
returns void as 'MODULE_PATHNAME', 'test_ereport'
language c;

/* create view to simplify usage of get_log function */
create view pg_log as
	select
		log_time, datid, d.datname, level::error_level, errno, message, detail, hint
	from get_log(true) as l
	left join pg_database as d on (l.datid = d.oid)
	left join pg_authid as u on (l.userid = u.oid);
