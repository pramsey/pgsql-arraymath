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
#include <fmgr.h>
#include <funcapi.h>
#include <utils/builtins.h>
#include <utils/array.h>
#include <catalog/pg_operator.h>
#include <catalog/namespace.h>
#include <utils/syscache.h>
#include <utils/typcache.h>
#include <nodes/value.h>


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
    bitmask <<= 1; \
    if (bitmask == 0x100) { \
        bitmap++; bitmask = 1; \
    } } while(0);

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
                               Oid element_type2, FmgrInfo *operfmgrinfo)
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

    fmgr_info(operform->oprcode, operfmgrinfo);
    ReleaseSysCache(opertup);

    return;
}

/*
* Apply an operator using an element over all the elements 
* of an array.
*/
static ArrayType * 
arraymath_array_oper_elem(ArrayType *array1, char *opname, ArrayType *array2)
{
    int ndims1 = ARR_NDIM(array1);
    int *dims1 = ARR_DIMS(array1);
    Oid element_type1 = ARR_ELEMTYPE(array1);
    int nitems1;
    FmgrInfo operfmgrinfo;
    
    elog(ERROR, "not implemeted yet");
    return NULL;
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
* Apply an operator over all the elements of a pair of arrays
* expanding to return an array of the same size as the largest
* input array.
*/
static ArrayType * 
arraymath_array_oper_array(ArrayType *array1, char *opname, ArrayType *array2)
{
    ArrayType *array_out;
    int	dims[1];
	int	lbs[1];
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

    /* Only 1D arrays for now */
    if ( ndims1 != 1 || ndims2 != 1 )
    {
        elog(ERROR, "only 1-dimensional arrays supported");
        return NULL;
    }

    /* What function works for these input types? Populate operfmgrinfo. */
    arraymath_fmgrinfo_from_optype(opname, element_type1, element_type2, &operfmgrinfo);

    /* What data type will the output array be? */
    rtype = get_fn_expr_rettype(&operfmgrinfo);
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
* Our only function, takes in two arrays and tries to add
* them together.
*/
Datum arr_plus_arr(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(array_add);
Datum arr_plus_arr(PG_FUNCTION_ARGS)
{
    ArrayType *array1 = PG_GETARG_ARRAYTYPE_P(0);
    ArrayType *array2 = PG_GETARG_ARRAYTYPE_P(1);
//    Oid collation = PG_GET_COLLATION();

    PG_RETURN_ARRAYTYPE_P(arraymath_array_oper_array(array1, "+", array2));
}


#if 0
/*
 * construct_array	--- simple method for constructing an array object
 *
 * elems: array of Datum items to become the array contents
 *		  (NULL element values are not supported).
 * nelems: number of items
 * elmtype, elmlen, elmbyval, elmalign: info for the datatype of the items
 *
 * A palloc'd 1-D array object is constructed and returned.  Note that
 * elem values will be copied into the object even if pass-by-ref type.
 *
 * NOTE: it would be cleaner to look up the elmlen/elmbval/elmalign info
 * from the system catalogs, given the elmtype.  However, the caller is
 * in a better position to cache this info across multiple uses, or even
 * to hard-wire values if the element type is hard-wired.
 */
ArrayType *
construct_array(Datum *elems, int nelems,
				Oid elmtype,
				int elmlen, bool elmbyval, char elmalign)
#endif




#if 0
    TypeCacheEntry *typentry;
    int                     typlen;
    bool            typbyval;
    char            typalign;
    char       *ptr1;
    char       *ptr2;
    bits8      *bitmap1;
    bits8      *bitmap2;
    int                     bitmask;
    int                     i;
    FunctionCallInfoData locfcinfo;

    if (element_type != ARR_ELEMTYPE(array2))
            ereport(ERROR,
                            (errcode(ERRCODE_DATATYPE_MISMATCH),
                             errmsg("cannot compare arrays of different element types")));

    /* fast path if the arrays do not have the same dimensionality */
    if (ndims1 != ndims2 ||
            memcmp(dims1, dims2, 2 * ndims1 * sizeof(int)) != 0)
            result = false;
    else
    {
                /*
                 * We arrange to look up the equality function only once per series of
                 * calls, assuming the element type doesn't change underneath us.  The
                 * typcache is used so that we have no memory leakage when being used
                 * as an index support function.
                 */
                typentry = (TypeCacheEntry *) fcinfo->flinfo->fn_extra;
                if (typentry == NULL ||
                        typentry->type_id != element_type)
                {
                        typentry = lookup_type_cache(element_type,
                                                                                 TYPECACHE_EQ_OPR_FINFO);
                        if (!OidIsValid(typentry->eq_opr_finfo.fn_oid))
                                ereport(ERROR,
                                                (errcode(ERRCODE_UNDEFINED_FUNCTION),
                                errmsg("could not identify an equality operator for type %s",
                                           format_type_be(element_type))));
                        fcinfo->flinfo->fn_extra = (void *) typentry;
                }
                typlen = typentry->typlen;
                typbyval = typentry->typbyval;
                typalign = typentry->typalign;

                /*
                 * apply the operator to each pair of array elements.
                 */
                InitFunctionCallInfoData(locfcinfo, &typentry->eq_opr_finfo, 2,
                                                                 collation, NULL, NULL);

                /* Loop over source data */
                nitems = ArrayGetNItems(ndims1, dims1);
                ptr1 = ARR_DATA_PTR(array1);
                ptr2 = ARR_DATA_PTR(array2);
                bitmap1 = ARR_NULLBITMAP(array1);
                bitmap2 = ARR_NULLBITMAP(array2);
                bitmask = 1;                    /* use same bitmask for both arrays */
                for (i = 0; i < nitems; i++)
                {
                        Datum           elt1;
                        Datum           elt2;
                        bool            isnull1;
                        bool            isnull2;
                        bool            oprresult;

                        /* Get elements, checking for NULL */
                        if ()
                        {
                                isnull1 = true;
                                elt1 = (Datum) 0;
                        }
                        else
                        {
                                isnull1 = false;
                                elt1 = fetch_att(ptr1, typbyval, typlen);
                                ptr1 = att_addlength_pointer(ptr1, typlen, ptr1);
                                ptr1 = (char *) att_align_nominal(ptr1, typalign);
                        }

                        if (bitmap2 && (*bitmap2 & bitmask) == 0)
                        {
                                isnull2 = true;
                                elt2 = (Datum) 0;
                        }
                        else
                        {
                                isnull2 = false;
                                elt2 = fetch_att(ptr2, typbyval, typlen);
                                ptr2 = att_addlength_pointer(ptr2, typlen, ptr2);
                                ptr2 = (char *) att_align_nominal(ptr2, typalign);
                        }
                        /* advance bitmap pointers if any */
                        bitmask <<= 1;
                        if (bitmask == 0x100)
                        {
                                if (bitmap1)
                                        bitmap1++;
                                if (bitmap2)
                                        bitmap2++;
                                bitmask = 1;
                        }

                        /*
                         * We consider two NULLs equal; NULL and not-NULL are unequal.
                         */
                        if (isnull1 && isnull2)
                                continue;
                        if (isnull1 || isnull2)
                        {
                                result = false;
                                break;
                        }

                        /*
                         * Apply the operator to the element pair
                         */
                        locfcinfo.arg[0] = elt1;
                        locfcinfo.arg[1] = elt2;
                        locfcinfo.argnull[0] = false;
                        locfcinfo.argnull[1] = false;
                        locfcinfo.isnull = false;
                        oprresult = DatumGetBool(FunctionCallInvoke(&locfcinfo));
                        if (!oprresult)
                        {
                                                            result = false;
                                                            break;
                                                    }
                                            }
                                    }

                                    /* Avoid leaking memory when handed toasted input. */
                                    PG_FREE_IF_COPY(array1, 0);
                                    PG_FREE_IF_COPY(array2, 1);

                                    PG_RETURN_BOOL(result);
                            }
#endif

	
	





