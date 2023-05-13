CREATE EXTENSION arraymath;

SELECT ARRAY[1,2,3,4] @< 4
	AS array_lt_constant;

SELECT ARRAY[3.4,5.6,7.6] @* 8.1
	AS array_times_constant;

SELECT ARRAY[1,2] @+ ARRAY[3,4]
	AS array_plus_array;

SELECT ARRAY[1,2,3,4,5,6] @* ARRAY[1,2]
	AS array_times_array;

SELECT ARRAY[1,1,1,1] @< ARRAY[0,2]
	AS array_lt_array;

SELECT ARRAY[1,2,3] @= ARRAY[3,2,1]
	AS array_eq_array;

WITH a AS (
SELECT array_agg(a) AS b FROM generate_series(1,100) a
)
SELECT b @+ b
	AS array_plus_array_lg FROM a;

WITH a AS (
SELECT array_agg(a) AS b FROM generate_series(1,100) a
)
SELECT b @+ ARRAY[0,1]
	AS array_plus_array_sm FROM a;

SELECT ARRAY[1,1,1,1] @< ARRAY[1,NULL,1,1]
	AS array_lt_array_null;

SELECT ARRAY[NULL] @< ARRAY[NULL]
 	AS array_lt_array_nullnull;

SELECT ARRAY[[1,2,3],[1,2]] @+ 1
	AS array_2d_err;

SELECT ARRAY[]::integer[] @< ARRAY[NULL]::integer[]
	AS array_null_error_a;

SELECT ARRAY[NULL]::integer[] @< ARRAY[]::integer[]
	AS array_null_error_b;

WITH a AS (
SELECT array_cat(array_agg(a), ARRAY[NULL,NULL]::int4[]) AS b FROM generate_series(1,10) a
)
SELECT
	array_sum(b) AS array_sum,
	array_min(b) AS array_min,
	array_max(b) AS array_max,
	array_avg(b) AS array_avg,
	array_median(b) AS array_median,
	array_sort(b) AS array_sort,
	array_sort(b, true) AS array_rsort
	FROM a;


