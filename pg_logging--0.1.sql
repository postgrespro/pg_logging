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

create type log_item as (
	level		int,
	errno		int,
	message		text,
	detail		text,
	hint		text,
	position	int
);

create or replace function get_log(
	flush	bool	/* always true for now */
)
returns log_item as 'MODULE_PATHNAME', 'get_logged_data'
language c;

create or replace function flush_log()
returns void as 'MODULE_PATHNAME', 'flush_logged_data'
language c;

/* create view to simplify usage of get_log function */
create view pg_log as
	select
		level::error_level, errno, message, detail, hint
	from get_log(true);
