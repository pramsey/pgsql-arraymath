pgsql-arraymath
===============

Enabling in a database
-------------------------
``
CREATE EXTENSION arraymath;
``

Functions and operators for element-by-element math and logic on arrays. The operators are all the usual ones, but prefixed by ``@`` to indicate their element-by-element nature.

* ``@=`` element-by-element equality, returns boolean[]
* ``@<`` element-by-element less than, returns boolean[]
* ``@>`` element-by-element greater than, returns boolean[]
* ``@<=`` element-by-element less than or equals, returns boolean[]
* ``@>=`` element-by-element greater than or equals, returns boolean[]
* ``@+`` element-by-element addition
* ``@-`` element-by-element subtraction
* ``@*`` element-by-element multiplication
* ``@/`` element-by-element division

Array versus Constant
---------------------

If you apply the operators with an array on one side and a constant on the other, the constant will be applied to all the elements of the array. For example:

    SELECT ARRAY[1,2,3,4] @< 4;
    
      {t,t,t,f}
    
    SELECT ARRAY[3.4,5.6,7.6] @* 8.1;
    
      {27.54,45.36,61.56}
     
Array versus Array
------------------

If you apply the operators with an array on both sides, the operator will be applied to each element pairing in turn, returning an array as long as the larger of the two inputs. Where the shorter array runs out of elements, the process will simply move back to the start of the array. For example:

    SELECT ARRAY[1,2] @+ ARRAY[3,4];
    
      {4,6}
      
    SELECT ARRAY[1,2,3,4,5,6] @* ARRAY[1,2];
    
      {1,4,3,8,5,12}
      
    SELECT ARRAY[1,1,1,1] @< ARRAY[0,2];
    
      {f,t,f,t}

    SELECT ARRAY[1,2,3] @= ARRAY[3,2,1];

      {f,t,f}
      
      
