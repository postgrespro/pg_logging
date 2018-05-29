create or replace function get_log(
	flush	bool
)
returns text as 'MODULE_PATHNAME', 'get_logged_data'
language c;

create view pg_log as select * from get_log(true);

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
	internallength = 1,
    passedbyvalue,
    alignment = char
);
