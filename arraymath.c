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



/**
* Our only function, takes in two arrays and tries to add
* them together.
*/
Datum array_add(PG_FUNCTION_ARGS);
PG_FUNCTION_INFO_V1(array_add);
Datum array_add(PG_FUNCTION_ARGS)
{
    ArrayType  *array1 = PG_GETARG_ARRAYTYPE_P(0);
    ArrayType  *array2 = PG_GETARG_ARRAYTYPE_P(1);
    Oid collation = PG_GET_COLLATION();
    int ndims1 = ARR_NDIM(array1);
    int ndims2 = ARR_NDIM(array2);
    int *dims1 = ARR_DIMS(array1);
    int *dims2 = ARR_DIMS(array2);
    Oid element_type1 = ARR_ELEMTYPE(array1);
    Oid element_type2 = ARR_ELEMTYPE(array2);
    const char *opname = "+";
    Oid operator_oid;
    
    int nitems1;
    int nitems2;

    // extern Oid      OpernameGetOprid(List *names, Oid oprleft, Oid oprright);
    // extern bool OperatorIsVisible(Oid oprid);
    
    //opertup = SearchSysCache1(OPEROID, ObjectIdGetDatum(oprid));
    // if (HeapTupleIsValid(opertup))
    //             Form_pg_operator operform = (Form_pg_operator) GETSTRUCT(opertup);
    //    
    // functionId = operform->oprcode;
    
    
    /*
     * This routine fills a FmgrInfo struct, given the OID
     * of the function to be called.
     */
    // extern void fmgr_info(Oid functionId, FmgrInfo *flinfo);

    // #define FunctionCall1(flinfo, arg1) \
    
    
    operator_oid = OpernameGetOprid(list_make1(opname), element_type1, element_type2);
    
    
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
                        if (bitmap1 && (*bitmap1 & bitmask) == 0)
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


}
	
	





