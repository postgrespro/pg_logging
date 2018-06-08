pg_logging
=================

PostgreSQL logging interface.

Installation
-------------

	# make sure that directory with pg_config in PATH or specify it with PG_CONFIG variable
	make install

	# install the extension
	> CREATE EXTENSION pg_logging;

Available functions
--------------------

	get_log(
		flush				true
	)

This function is used to fetch the logged information. The information is
similar to the data that postgres writes to log files.

`flush` means that fetched data will not be returned on next query calls.

The logs are stored in the ring buffer which means that non fetched data will
be rewritten in the buffer wraparounds. Since reading position should be
accordingly moved on each rewrite it could slower down the database.

Options
---------

	pg_logging.buffer_size (10240) - size of internal ring buffer in kilobytes.
	pg_logging.enabled (on) - enables or disables the logging.
	pg_logging.ignore_statements (off) - skip statements lines if `log_statement=all`
	pg_logging.set_query_fields (on) - set query and query_pos fields.
