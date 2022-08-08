/***********************************************************************
 *
 * Project:  Array Math
 * Purpose:  Main file.
 *
 ***********************************************************************
 * Copyright 2012 Paul Ramsey <pramsey@cleverelephant.ca>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 ***********************************************************************/

#define ARRAYMATH_VERSION "1.0"


/* PostgreSQL */
#include <postgres.h>
#include <pg_config.h>
#include <fmgr.h>
#include <funcapi.h>

/* Include for VARATT_EXTERNAL_GET_POINTER */
#if PG_VERSION_NUM < 130000
#  include <access/tuptoaster.h>
#else
#  include <access/detoast.h>
#endif

#include <catalog/namespace.h>
#include <catalog/pg_operator.h>
#include <catalog/pg_type.h>
#include <nodes/value.h>
#include <utils/array.h>
#include <utils/builtins.h>
#include <utils/syscache.h>
#include <utils/typcache.h>



/**********************************************************************
* PostgreSQL initialization routines
*/

/* Set up PgSQL */
PG_MODULE_MAGIC;

/* Startup */
void _PG_init(void);
void _PG_init(void)
{
    elog(NOTICE, "Hello from ArrayMath %s", ARRAYMATH_VERSION);
    
}

/* Tear-down */
void _PG_fini(void);
void _PG_fini(void)
{
    elog(NOTICE, "Goodbye from ArrayMath %s", ARRAYMATH_VERSION);
}


/**********************************************************************
* Utility macros
*/

#define ARRISEMPTY(x)  (ARRNELEMS(x) == 0)

#define BITMAP_GET(bitmap, i) (bitmap && (bitmap[(i)/sizeof(bits8)] & (1 << ((i) % sizeof(bits8)))))

#define BITMAP_INCREMENT(bitmap, bitmask) do { \
    if (bitmap) { \
        bitmask <<= 1; \
        if (bitmask == 0x100) { \
            bitmap++; bitmask = 1; \
    } } } while(0);

#define BITMAP_ISNULL(bitmap, bitmask) (bitmap && (*bitmap & bitmask) == 0)
    


/**********************************************************************
* Functions
*/
    

/*
* Given an operator symbol ("+", "-", "=" etc) and type element types, 
* try to look up the appropriate function to do element level operations of 
* that type.
*/
static void 
arraymath_fmgrinfo_from_optype(const char *opstr, Oid element_type1, 
                               Oid element_type2, FmgrInfo *operfmgrinfo, Oid *return_type)
{
    Oid operator_oid;    
    HeapTuple opertup;
    Form_pg_operator operform;

    /* Look up the operator Oid that corresponts to this combination */
    /* of symbol and data types */
    operator_oid = OpernameGetOprid(list_make1(makeString(pstrdup(opstr))), element_type1, element_type2);
    if ( ! (operator_oid && OperatorIsVisible(operator_oid)) )
    {
        elog(ERROR, "operator does not exist");
    }

    /* Lookup the function associated with the operator Oid */
    opertup = SearchSysCache1(OPEROID, ObjectIdGetDatum(operator_oid));
    if (! HeapTupleIsValid(opertup))
    {
        elog(ERROR, "cannot find operator heap tuple");
    }

    operform = (Form_pg_operator) GETSTRUCT(opertup);
    *return_type = operform->oprresult;

    fmgr_info(operform->oprcode, operfmgrinfo);
    ReleaseSysCache(opertup);

    return;
}


/*
* Read type information for a given element type.
*/
static TypeCacheEntry * 
arraymath_typentry_from_type(Oid element_type)
{
    TypeCacheEntry *typentry;
    typentry = lookup_type_cache(element_type, 0);
    if (!typentry)
    {
        elog(ERROR, "unable to lookup element type info for %s", format_type_be(element_type));
    }
    return typentry;    
}


/*
* Apply an operator using an element over all the elements 
* of an array.
*/
static ArrayType * 
arraymath_array_oper_elem(ArrayType *array1, const char *opname, Datum element2, Oid element_type2)
{
    ArrayType *array_out;
    int    dims[1];
    int    lbs[1];
    Datum *elems;
    bool *nulls;
    
    int ndims1 = ARR_NDIM(array1);
    int *dims1 = ARR_DIMS(array1);
    Oid element_type1 = ARR_ELEMTYPE(array1);
    Oid rtype; 
    int nelems, n = 0;
    FmgrInfo operfmgrinfo;
    TypeCacheEntry *info1, *tinfo;
    ArrayIterator iterator1;
    Datum element1;
    bool isnull1;

    /* Only 1D arrays for now */
    if (ndims1 != 1)
    {
        elog(ERROR, "only 1-dimensional arrays supported");
        return NULL;
    }

    /* What function works for these input types? Populate operfmgrinfo. */
    /* What data type will the output array be? */
    arraymath_fmgrinfo_from_optype(opname, element_type1, element_type2, &operfmgrinfo, &rtype);

    /* How big is the output array? */
    nelems = ArrayGetNItems(ndims1, dims1);

    /* If input is empty, return empty */
    if (nelems == 0)
    {
        return construct_empty_array(rtype);
    }
    
    /* Learn more about the input array */
    info1 = arraymath_typentry_from_type(element_type1);

#if PG_VERSION_NUM >= 90500
    iterator1 = array_create_iterator(array1, 0, NULL);
#else
    iterator1 = array_create_iterator(array1, 0);
#endif

    /* Allocate space for output data */
    elems = palloc(sizeof(Datum)*nelems);
    nulls = palloc(sizeof(bool)*nelems);

    while (array_iterate(iterator1, &element1, &isnull1))
    {
        if (isnull1)
        {
            nulls[n] = true;
            elems[n] = (Datum) 0;
        }
        else
        {
            /* Apply the operator */
            nulls[n] = false;
            elems[n] = FunctionCall2(&operfmgrinfo, element1, element2);
        }
        n++;
    }

    /* Build 1-d output array */
    tinfo = arraymath_typentry_from_type(rtype);
    dims[0] = nelems;
    lbs[0] = 1;
    array_out = construct_md_array(elems, nulls, 1, dims, lbs, rtype, tinfo->typlen, tinfo->typbyval, tinfo->typalign);
    
    /* Output is supposed to be a copy, so free the inputs */
    pfree(elems);
    pfree(nulls);
    
    /* Make sure we haven't been given garbage */
    if (!array_out)
    {
        elog(ERROR, "unable to construct output array");
        return NULL;
    }
    
    return array_out;
}

/*
* Apply an operator over all the elements of a pair of arrays
* expanding to return an array of the same size as the largest
* input array.
*/
static ArrayType * 
arraymath_array_oper_array(ArrayType *array1, const char *opname, ArrayType *array2)
{
    ArrayType *array_out;
    int    dims[1];
    int    lbs[1];
    Datum *elems;
    bool *nulls;
    
    int ndims1 = ARR_NDIM(array1);
    int ndims2 = ARR_NDIM(array2);
    int *dims1 = ARR_DIMS(array1);
    int *dims2 = ARR_DIMS(array2);
    char *ptr1, *ptr2;
    Oid element_type1 = ARR_ELEMTYPE(array1);
    Oid element_type2 = ARR_ELEMTYPE(array2);   
    Oid rtype; 
    int nitems1, nitems2;
    int nelems, n;
    bits8 *bitmap1, *bitmap2;
    int bitmask1, bitmask2;
    FmgrInfo operfmgrinfo;
    TypeCacheEntry *info1, *info2, *tinfo;

    if ( ndims1 == 0 && ndims2 == 1 )
    {
        return array2;
    }
    else if ( ndims1 == 1 && ndims2 == 0 )
    {
        return array1;
    }
    else if ( ndims1 == 0 && ndims2 == 0 )
    {
        return construct_empty_array(element_type1);
    }

    /* Only 1D arrays for now */
    if ( ndims1 != 1 || ndims2 != 1 )
    {
        elog(ERROR, "only 1-dimensional arrays supported");
        return NULL;
    }

    /* What function works for these input types? Populate operfmgrinfo. */
    /* What data type will the output array be? */
    arraymath_fmgrinfo_from_optype(opname, element_type1, element_type2, &operfmgrinfo, &rtype);
    tinfo = arraymath_typentry_from_type(rtype);

    /* How big is the output array? */
    nitems1 = ArrayGetNItems(ndims1, dims1);
    nitems2 = ArrayGetNItems(ndims2, dims2);
    nelems = Max(nitems1, nitems2);

    /* If either input is empty, return empty */
    if ( nitems1 == 0 || nitems2 == 0 )
    {
        return construct_empty_array(rtype);
    }
    
    /* Allocate space for output data */
    elems = palloc(sizeof(Datum)*nelems);
    nulls = palloc(sizeof(bool)*nelems);

    /* Learn more about the input arrays */
    info1 = arraymath_typentry_from_type(element_type1);
    info2 = arraymath_typentry_from_type(element_type2);
    
    /* Loop over all the items, re-using items from the shorter */
    /* array to apply to the longer */
    for( n = 0; n < nelems; n++ )
    {
        Datum elt1, elt2;
        int i1 = n % nitems1;
        int i2 = n % nitems2;
        bool isnull1, isnull2;

        /* Initialize array pointers at start of loop, and */
        /* on wrap-around */
        if ( i1 == 0 )
        {
            ptr1 = ARR_DATA_PTR(array1);
            bitmap1 = ARR_NULLBITMAP(array1);
            bitmask1 = 1;
        }
        
        if ( i2 == 0 )
        {
            ptr2 = ARR_DATA_PTR(array2);
            bitmap2 = ARR_NULLBITMAP(array2);
            bitmask2 = 1;
        }
        
        /* Check null status */
        isnull1 = BITMAP_ISNULL(bitmap1, bitmask1);
        isnull2 = BITMAP_ISNULL(bitmap2, bitmask2);
        
        /* Start with NULL values */
        elt1 = elt2 = (Datum) 0;
        
        if ( ! isnull1 )
        {
            /* Read the element value */
            elt1 = fetch_att(ptr1, info1->typbyval, info1->typlen);

            /* Move the pointer forward */
            ptr1 = att_addlength_pointer(ptr1, info1->typlen, ptr1);
            ptr1 = (char *) att_align_nominal(ptr1, info1->typalign);
        }

        if ( ! isnull2 )
        {
            /* Read the element value */
            elt2 = fetch_att(ptr2, info2->typbyval, info2->typlen);

            /* Move the pointer forward */
            ptr2 = att_addlength_pointer(ptr2, info2->typlen, ptr2);
            ptr2 = (char *) att_align_nominal(ptr2, info2->typalign);
        }

        /* NULL on either side of operator yields output NULL */
        if ( isnull1 || isnull2 )
        {
            nulls[n] = true;
            elems[n] = (Datum) 0;
        }
        else
        {
            nulls[n] = false;
            elems[n] = FunctionCall2(&operfmgrinfo, elt1, elt2);
        }
        
        BITMAP_INCREMENT(bitmap1, bitmask1);
        BITMAP_INCREMENT(bitmap2, bitmask2);
    }

    /* Build 1-d output array */
    dims[0] = nelems;
    lbs[0] = 1;
    array_out = construct_md_array(elems, nulls, 1, dims, lbs, rtype, tinfo->typlen, tinfo->typbyval, tinfo->typalign);
    
    /* Output is supposed to be a copy, so free the inputs */
    pfree(elems);
    pfree(nulls);
    
    /* Make sure we haven't been given garbage */
    if ( ! array_out )
    {
        elog(ERROR, "unable to construct output array");
        return NULL;
    }
    
    return array_out;
    
}

/*
* Compare two arrays.
*/
Datum array_compare_array(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(array_compare_array);
Datum array_compare_array(PG_FUNCTION_ARGS)
{
    ArrayType *array1 = PG_GETARG_ARRAYTYPE_P(0);
    ArrayType *array2 = PG_GETARG_ARRAYTYPE_P(1);
    text *operator = PG_GETARG_TEXT_P(2);
    char *opname = text_to_cstring(operator);
    ArrayType *arrayout;
    
    arrayout = arraymath_array_oper_array(array1, opname, array2);

    PG_FREE_IF_COPY(array1, 0);
    PG_FREE_IF_COPY(array2, 1);
    
    PG_RETURN_ARRAYTYPE_P(arrayout);
}


/*
* Operator on two arrays.
*/
Datum array_math_array(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(array_math_array);
Datum array_math_array(PG_FUNCTION_ARGS)
{
    ArrayType *array1 = PG_GETARG_ARRAYTYPE_P(0);
    ArrayType *array2 = PG_GETARG_ARRAYTYPE_P(1);
    text *operator = PG_GETARG_TEXT_P(2);
    char *opname = text_to_cstring(operator);
    ArrayType *arrayout;
    
    arrayout = arraymath_array_oper_array(array1, opname, array2);

    PG_FREE_IF_COPY(array1, 0);
    PG_FREE_IF_COPY(array2, 1);
    
    PG_RETURN_ARRAYTYPE_P(arrayout);
}



/*
* Compare an array to a constant element 
*/
Datum array_compare_value(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(array_compare_value);
Datum array_compare_value(PG_FUNCTION_ARGS)
{
    ArrayType *array1 = PG_GETARG_ARRAYTYPE_P(0);
    Datum element2 = PG_GETARG_DATUM(1);
    text *operator = PG_GETARG_TEXT_P(2);
    char *opname = text_to_cstring(operator);
    Oid element_type2;
    ArrayType *arrayout;
    
    element_type2 = get_fn_expr_argtype(fcinfo->flinfo, 1);
    arrayout = arraymath_array_oper_elem(array1, opname, element2, element_type2);

    PG_FREE_IF_COPY(array1, 0);
    PG_RETURN_ARRAYTYPE_P(arrayout);
}

/*
* Do math on an array using a constant element
*/
Datum array_math_value(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(array_math_value);
Datum array_math_value(PG_FUNCTION_ARGS)
{
    ArrayType *array1 = PG_GETARG_ARRAYTYPE_P(0);
    Datum element2 = PG_GETARG_DATUM(1);
    text *operator = PG_GETARG_TEXT_P(2);
    char *opname = text_to_cstring(operator);
    Oid element_type2;
    ArrayType *arrayout;
    
    element_type2 = get_fn_expr_argtype(fcinfo->flinfo, 1);
    arrayout = arraymath_array_oper_elem(array1, opname, element2, element_type2);

    PG_FREE_IF_COPY(array1, 0);
    PG_RETURN_ARRAYTYPE_P(arrayout);
}



/*
* Do sum of an array
*/
Datum array_sum(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(array_sum);
Datum
array_sum(PG_FUNCTION_ARGS)
{
    // Our arguments:
    ArrayType *vals;

    // The array element type:
    Oid valsType;

    // The array element type widths for our input array:
    int16 valsTypeWidth;

    // The array element type "is passed by value" flags (not really used):
    bool valsTypeByValue;

    // The array element type alignment codes (not really used):
    char valsTypeAlignmentCode;

    // The array contents, as PostgreSQL "Datum" objects:
    Datum *valsContent;

    // List of "is null" flags for the array contents (not used):
    bool *valsNullFlags;

    // The size of the input array:
    int valsLength;

    Datum v = (Datum)0;
    int i;

    if (PG_ARGISNULL(0)) 
    {
        ereport(ERROR, (errmsg("Null arrays not accepted")));
    }

    vals = PG_GETARG_ARRAYTYPE_P(0);

    if (ARR_NDIM(vals) == 0) 
    {
        PG_RETURN_NULL();
    }
    if (ARR_NDIM(vals) > 1) 
    {
        ereport(ERROR, (errmsg("One-dimesional arrays are required")));
    }

    // Determine the array element types.
    valsType = ARR_ELEMTYPE(vals);

    if (valsType != INT2OID &&
        valsType != INT4OID &&
        valsType != INT8OID &&
        valsType != FLOAT4OID &&
        valsType != FLOAT8OID) 
    {
        ereport(ERROR, (errmsg("Sum subject must be SMALLINT, INTEGER, BIGINT, REAL, or DOUBLE PRECISION values")));
    }

    valsLength = (ARR_DIMS(vals))[0];

    // Empty length still return 0
    if (valsLength == 0)
    {
        goto END;
    } 

    

    // Get + operator FmgrInfo
    FmgrInfo operfmgrinfo;
    Oid rtype;
    const char* op = "+";

    TypeCacheEntry *info;

    arraymath_fmgrinfo_from_optype(op, valsType, valsType, &operfmgrinfo, &rtype);



    // Iterator
    ArrayIterator iterator;

    // Is Null boolean
    bool isnull;

    // Element
    Datum element;


#if PG_VERSION_NUM >= 90500
    iterator = array_create_iterator(vals, 0, NULL);
#else
    iterator = array_create_iterator(vals, 0);
#endif

    
    while (array_iterate(iterator, &element, &isnull))
    {
        if (!isnull)
        {
            /* Apply the operator */
            v = FunctionCall2(&operfmgrinfo, element, v);
        }
        
    }


    END:
        PG_RETURN_DATUM(v);

}
