CREATE EXTENSION arraymath;

SELECT ARRAY[1,2,3,4] @< 4;
SELECT ARRAY[3.4,5.6,7.6] @* 8.1;

SELECT ARRAY[1,2] @+ ARRAY[3,4];
SELECT ARRAY[1,2,3,4,5,6] @* ARRAY[1,2];
SELECT ARRAY[1,1,1,1] @< ARRAY[0,2];
SELECT ARRAY[1,2,3] @= ARRAY[3,2,1];

WITH a AS (
SELECT array_agg(a) AS b FROM generate_series(1,100) a
)
SELECT b @+ b FROM a;

WITH a AS (
SELECT array_agg(a) AS b FROM generate_series(1,100) a
)
SELECT b @+ ARRAY[0,1] FROM a;

SELECT ARRAY[1,1,1,1] @< ARRAY[1,NULL,1,1];

SELECT ARRAY[NULL] @< ARRAY[NULL];

SELECT ARRAY[[1,2,3],[1,2]] @+ 1;

SELECT ARRAY[]::integer[] @< ARRAY[NULL]::integer[];
