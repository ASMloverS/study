#pragma once
/*
 * NaN-boxing implementation for MsValue.
 * Included by value.h when MS_NAN_BOXING=1.
 *
 * Encoding (64-bit IEEE 754 NaN payload):
 *   [ sign:1 | exp:11 | quiet:1 | type:3 | payload:48 ]
 *     63       62-52     51        50-48    47-0
 *
 * Tag map:
 *   double (non-NaN) -- normal IEEE 754 double
 *   nil              0x7FFC000000000000  payload=0
 *   false            0x7FFD000000000000  payload=0
 *   true             0x7FFD000000000001  payload=1
 *   int  (+-2^31)    0x7FFE000000000000  payload=low-32 bits (sign-extended)
 *   object ptr       0x7FFF000000000000  payload=low-48 bits (user-space ptr)
 *
 * x86_64/ARM64 user-space pointers fit in 48 bits -- safe on all supported OSes.
 * MSVC fallback: compile with MS_NAN_BOXING=0 to keep the 16-byte tagged union.
 */

#include "ms/common.h"
#include <stdint.h>
#include <string.h>

typedef uint64_t MsValue;

/* ---- tag constants ---- */
#define MS_NAN_MASK  UINT64_C(0x7FFC000000000000)
#define MS_TAG_NIL   UINT64_C(0x7FFC000000000000)
#define MS_TAG_FALSE UINT64_C(0x7FFD000000000000)
#define MS_TAG_TRUE  UINT64_C(0x7FFD000000000001)
#define MS_TAG_INT   UINT64_C(0x7FFE000000000000)
#define MS_TAG_OBJ   UINT64_C(0x7FFF000000000000)
#define MS_QNAN      UINT64_C(0x7FF8000000000000)  /* canonical quiet NaN */

/* ---- predicates ---- */
static inline bool MS_IS_DOUBLE(MsValue v) { return (v & MS_NAN_MASK) != MS_NAN_MASK; }
static inline bool MS_IS_NIL(MsValue v)    { return v == MS_TAG_NIL; }
static inline bool MS_IS_BOOL(MsValue v)   { return (v & ~(uint64_t)1) == MS_TAG_FALSE; }
static inline bool MS_IS_TRUE(MsValue v)   { return v == MS_TAG_TRUE; }
/* High 32 bits of MS_TAG_INT are 0x7FFE0000; lies inside quiet-NaN space,
   never produced by normal double arithmetic. */
static inline bool MS_IS_INT(MsValue v)    { return (v >> 32) == (MS_TAG_INT >> 32); }
/* High 16 bits 0x7FFF; distinguishable from int (0x7FFE) and normal doubles. */
static inline bool MS_IS_OBJECT(MsValue v) {
    return (v & MS_TAG_OBJ) == MS_TAG_OBJ && !MS_IS_INT(v);
}
/* Numeric = double or int */
static inline bool MS_IS_NUMBER(MsValue v) { return MS_IS_DOUBLE(v); }
static inline bool ms_is_numeric_nan(MsValue v) {
    return MS_IS_DOUBLE(v) || MS_IS_INT(v);
}
#define MS_IS_NUMERIC(v) ms_is_numeric_nan(v)

/* Forward declaration -- defined in object.h; avoid circular include. */
typedef struct MsObject MsObject;

/* ---- accessors ---- */
static inline double    MS_AS_NUMBER(MsValue v) {
    double d; memcpy(&d, &v, 8); return d;
}
/* Sign-extend the low 32 bits back to ms_i64. */
static inline ms_i64    MS_AS_INT(MsValue v) {
    return (ms_i64)(int32_t)(uint32_t)(v & UINT64_C(0xFFFFFFFF));
}
static inline MsObject* MS_AS_OBJECT(MsValue v) {
    return (MsObject*)(uintptr_t)(v & UINT64_C(0x0000FFFFFFFFFFFF));
}
static inline bool      MS_AS_BOOL(MsValue v)   { return (bool)(v & 1u); }

/* ms_as_double: promote int to double for mixed arithmetic */
static inline double ms_as_double(MsValue v) {
    return MS_IS_INT(v) ? (double)MS_AS_INT(v) : MS_AS_NUMBER(v);
}

/* ---- constructors ---- */
static inline MsValue MS_NIL_VAL(void)        { return MS_TAG_NIL; }
static inline MsValue MS_BOOL_VAL(bool b)     { return b ? MS_TAG_TRUE : MS_TAG_FALSE; }

/* NaN normalisation: arithmetic NaN payloads must not collide with our tags.
   Unify all NaNs to MS_QNAN (0x7FF8...) which sits below the tag range. */
static inline MsValue MS_NUMBER_VAL(double d) {
    uint64_t bits; memcpy(&bits, &d, 8);
    if ((bits & MS_NAN_MASK) == MS_NAN_MASK) return MS_QNAN;
    return bits;
}

/* INT only stores +-2^31.  Values outside that range fall back to double.
   Precision note: double covers +-2^53 exactly; silent degradation above 2^53. */
static inline MsValue MS_INT_VAL(ms_i64 n) {
    if (n >= INT32_MIN && n <= INT32_MAX)
        return MS_TAG_INT | (uint64_t)(uint32_t)(int32_t)n;
    return MS_NUMBER_VAL((double)n);
}

/* Accept any pointer type (subclasses of MsObject) via macro cast. */
#define MS_OBJ_VAL(p) (MS_TAG_OBJ | (uint64_t)(uintptr_t)(MsObject*)(p))
