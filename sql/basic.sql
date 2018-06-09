create schema logging;
create extension pg_logging schema logging;

set log_statement=none;
set pg_logging.buffer_position = 0;
set pg_logging.buffer_size = 2000;

select logging.flush_log();

create view logs as
	select level, line_num, appname, errno, errcode,
			message, detail, detail_log, hint, context,
			domain, internalpos, internalquery, remote_host, command_tag,
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
select * from logs;

reset log_statement;
drop extension pg_logging cascade;
