# pgsql-arraymath

An extension for element-by-element operations on PostgreSQL arrays with a integer, float or numeric data type.

## Enabling in a database

```sql
CREATE EXTENSION arraymath;
```

The operators are all the usual ones, but prefixed by ``@`` to indicate their element-by-element nature.

* `@=` element-by-element equality, returns boolean[]
* `@<` element-by-element less than, returns boolean[]
* `@>` element-by-element greater than, returns boolean[]
* `@<=` element-by-element less than or equals, returns boolean[]
* `@>=` element-by-element greater than or equals, returns boolean[]
* `@+` element-by-element addition
* `@-` element-by-element subtraction
* `@*` element-by-element multiplication
* `@/` element-by-element division

The functions are prefixed by `array_`.

* `array_sum(anyarray)` sums up all the elements
* `array_avg(anyarray)` returns float average of all elements
* `array_min(anyarray)` returns minimum of all elements
* `array_max(anyarray)` returns maximum of all elements
* `array_med(anyarray)` returns the median of all elements
* `array_sort(anyarray)` sorts the array from smallest to largest
* `array_rsort(anyarray)` sorts the array from largest to smallest


## Array versus Constant

If you apply the operators with an array on one side and a constant on the other, the constant will be applied to all the elements of the array. For example:

```sql
SELECT ARRAY[1,2,3,4] @< 4;
```
```
{t,t,t,f}
```
```sql
SELECT ARRAY[3.4,5.6,7.6] @* 8.1;
```
```
{27.54,45.36,61.56}
```

## Array versus Array

If you apply the operators with an array on both sides, the operator will be applied to each element pairing in turn, returning an array as long as the larger of the two inputs. Where the shorter array runs out of elements, the process will simply move back to the start of the array. For example:

```
    SELECT ARRAY[1,2] @+ ARRAY[3,4];
    
      {4,6}
      
    SELECT ARRAY[1,2,3,4,5,6] @* ARRAY[1,2];
    
      {1,4,3,8,5,12}
      
    SELECT ARRAY[1,1,1,1] @< ARRAY[0,2];
    
      {f,t,f,t}

    SELECT ARRAY[1,2,3] @= ARRAY[3,2,1];

      {f,t,f}
```

