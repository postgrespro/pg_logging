# contrib/pg_logging/Makefile

MODULE_big = pg_logging
OBJS= pg_logging.o $(WIN32RES)

EXTENSION = pg_logging
DATA = pg_logging--0.1.sql
PGFILEDESC = "PostgreSQL logging interface"

REGRESS = basic

ifndef PG_CONFIG
PG_CONFIG = pg_config
endif

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
