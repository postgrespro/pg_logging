[![Build Status](https://travis-ci.org/postgrespro/pg_logging.svg?branch=master)](https://travis-ci.org/postgrespro/pg_logging)
[![GitHub license](https://img.shields.io/badge/license-PostgreSQL-blue.svg)](https://raw.githubusercontent.com/postgrespro/pg_logging/master/LICENSE)

pg_logging
=================

PostgreSQL logging interface.

Installation
-------------

    # make sure that directory with pg_config in PATH or specify it with PG_CONFIG variable
    make install

    # in postgresql.conf add:
    shared_preload_libraries = 'pg_logging'

    # install the extension
    > CREATE EXTENSION pg_logging;

Available functions
--------------------

    get_log(
        flush               bool default true
    )

    get_log(
        from_position       int
    )

This function is used to fetch the logged information. The information is
similar to the data that postgres writes to log files.

`flush` means that fetched data will not be returned on next query calls. By
default it's true.

`from_position` is used as fail-safe case, when a client specifies until
which position it already has data. This position should be equal to
`position` field from `log_item`. If there was wraparound there is a chance
that the position will be invalid and query will raise an error. In this case
the client should use `get_log(flush bool)` function (and possibly increase
the ring buffer size).

Logs are stored in the ring buffer which means that non fetched data will
be rewritten in the buffer wraparounds. Since reading position should be
accordingly moved on each rewrite it could slower down the database.

`get_log` function returns rows of `log_item` type. `log_item` is specified as:

    create type log_item as (
        log_time            timestamp with time zone,
        level               int,
        pid                 int,
        line_num            bigint,                     /* log line number */
        appname             text,
        start_time          timestamp with time zone,   /* backend start time */
        datid               oid,                        /* database id */
        errno               int,
        errcode             int,
        errstate            text,
        message             text,
        detail              text,
        detail_log          text,
        hint                text,
        context             text,
        context_domain      text,
        domain              text,
        internalpos         int,
        internalquery       text,
        userid              oid,
        remote_host         text,
        command_tag         text,
        vxid                text,                       /* virtual transaction id */
        txid                bigint,                     /* transaction id */
        query               text,
        query_pos           int,
        position            int
    );

`error_level` type
-------------------

The extension installs special type called `error_level` which can be used to
get textual representation of `level` field from `log_item`. To do that
just add something like `level::error_level` to the columns list in your query.


Options
---------

    pg_logging.buffer_size (10240) - size of internal ring buffer in kilobytes.
    pg_logging.enabled (on) - enables or disables the logging.
    pg_logging.ignore_statements (off) - skip statements lines if `log_statement=all`
    pg_logging.set_query_fields (on) - set query and query_pos fields.
