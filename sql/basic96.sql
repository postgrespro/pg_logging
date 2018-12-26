create schema logging;
create extension pg_logging schema logging;

set log_statement=none;
set pg_logging.buffer_size = 2000;

select logging.flush_log();

create view logs as
	select level, appname, errcode,
			message, detail, detail_log, hint, context,
			errstate, internalpos, internalquery, remote_host, command_tag,
			query, query_pos
	from logging.get_log();

create view logs2 as
	select level::logging.error_level, appname, errcode,
			message, detail, detail_log, hint, context,
			errstate, internalpos, internalquery, remote_host, command_tag,
			query, query_pos
	from logging.get_log();

select * from logs;

select 1/0;
select 'aaaaa'::int;
select * from logs;

select repeat('aaaaaaaaa', 20)::int;
select repeat('aaaaaaaaa', 20)::int;
select repeat('aaaaaaaaa', 20)::int;
select repeat('aaaaaaaaa', 20)::int;
select * from logs;

select logging.test_ereport('error', 'one', 'two', 'three');
select * from logs2;

/* cleanup */
select logging.flush_log();

show pg_logging.minlevel;
set pg_logging.minlevel = error;

select logging.test_ereport('error', 'notice1', 'detail', 'hint');
select logging.test_ereport('error', 'notice2', 'detail', 'hint');
select logging.test_ereport('error', 'notice3', 'detail', 'hint');
select level, message, position from logging.get_log(false);
select level, message, position from logging.get_log(268);
select level, message, position from logging.get_log(false);
select level, message, position from logging.get_log(536);
select level, message, position from logging.get_log(false);
select level, message, position from logging.get_log(1000);
select level, message, position from logging.get_log(false);

reset log_statement;
drop extension pg_logging cascade;
