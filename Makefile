
MODULE_big = arraymath
OBJS = arraymath.o
EXTENSION = arraymath
REGRESS = arraymath
EXTRA_CLEAN =
PG_CONFIG = pg_config

DATA = \
	arraymath--1.0.sql \
	arraymath--1.1.sql \
	arraymath--1.0--1.1.sql

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

