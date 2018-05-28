create or replace function get_log(
	flush	bool
)
returns text as 'MODULE_PATHNAME', 'pg_logging_get_log'
language c;

create view pg_log as
	select * from get_log(true);

create view pg_errorlog as
	select * from get_log(true) where level = 'error'::pg_logging_errlevel;

create type ErrorLevel;

create or replace function errlevel_out(ErrorLevel)
returns cstring as 'MODULE_PATHNAME'
language c strict immutable parallel safe;

create or replace function errlevel_in(cstring)
returns cstring as 'MODULE_PATHNAME'
language c strict immutable parallel safe;

create or replace function errlevel_eq(ErrorLevel, ErrorLevel)
returns cstring as 'MODULE_PATHNAME'
language c strict immutable parallel safe;

create type ErrorLevel (
	input = errlevel_in,
	output = errlevel_out,
	internallength = 1,
    passedbyvalue,
    alignment = char
);
