create schema logging;
create extension pg_logging schema logging;

set pg_logging.buffer_position = 0;
set pg_logging.buffer_size = 2000;
set pg_logging.buffer_size = 1;
show pg_logging.buffer_position;

select logging.flush_log();
select * from logging.pg_log;

select 1/0;
show pg_logging.buffer_position;
select 'aaaaa'::int;
show pg_logging.buffer_position;
select * from logging.get_log(true);
