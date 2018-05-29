# contrib/pg_logging/Makefile

MODULE_big = pg_logging
OBJS= pg_logging.o errlevel.o pl_funcs.o $(WIN32RES)

EXTENSION = pg_logging
DATA = pg_logging--0.1.sql
PGFILEDESC = "PostgreSQL logging interface"

REGRESS = basic

ifndef PG_CONFIG
PG_CONFIG = pg_config
endif

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

errlevel.c: errlevel.gperf
	gperf errlevel.gperf --null-strings --global-table --output-file=errlevel.c --word-array-name=errlevel_wordlist
	sed -i.bak -e 's/static struct ErrorLevel errlevel_wordlist/struct ErrorLevel errlevel_wordlist/g' errlevel.c
	rm errlevel.c.bak
