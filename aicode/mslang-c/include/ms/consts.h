#pragma once
#define MS_STACK_MAX        256
#define MS_FRAMES_MAX        64
#define MS_MAX_UPVALUES     256
#define MS_MAX_CONSTANTS    65536
#define MS_GC_NURSERY_SIZE  (256 * 1024)
#define MS_GC_PROMOTE_AGE   3
#define MS_GC_INCR_WORK     64
#define MS_IC_PIC_SIZE      4
#define MS_SBO_FIELDS       8
#define MS_POOL_SLAB_SIZE   64
#define MS_STACK_SIZE       (MS_FRAMES_MAX * MS_STACK_MAX)
#define MS_VERSION          "0.1.0"
#define MS_MAX_INTERP_DEPTH 8
