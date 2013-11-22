-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION arraymath" to load this file. \quit

CREATE OR REPLACE FUNCTION array_add(arr1 ANYARRAY, arr2 ANYARRAY)
	RETURNS integer
	AS 'MODULE_PATHNAME'
	LANGUAGE C;

