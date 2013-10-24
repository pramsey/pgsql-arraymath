
MODULE_big = arraymath
OBJS = arraymath.o
EXTENSION = arraymath
DATA = arraymath--1.0.sql
REGRESS = arraymath
EXTRA-CLEAN =

PG_CONFIG = pg_config

PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)

