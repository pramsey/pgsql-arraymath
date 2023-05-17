
MODULE_big = arraymath
OBJS = arraymath.o
EXTENSION = arraymath
REGRESS = arraymath
EXTRA_CLEAN =

DATA = \
	arraymath--1.0.sql \
	arraymath--1.1.sql \
	arraymath--1.0--1.1.sql

PG_CONFIG = pg_config

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

