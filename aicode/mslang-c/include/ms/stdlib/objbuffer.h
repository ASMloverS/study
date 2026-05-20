#ifndef MS_OBJBUFFER_H
#define MS_OBJBUFFER_H

#include "ms/object.h"
#include <stdint.h>
#include <stddef.h>

struct MsObjBuffer {
    MsObject obj;   /* type = MS_OBJ_BUFFER */
    uint8_t* data;
    size_t   len;
    size_t   cap;
};

typedef struct MsObjBuffer MsObjBuffer;

struct MsVM;
typedef struct MsObjString MsObjString;

MsObjBuffer* ms_obj_buffer_new(struct MsVM* vm, size_t initial_cap);
MsObjBuffer* ms_obj_buffer_from_bytes(struct MsVM* vm, const uint8_t* bytes, size_t len);
bool         ms_objbuffer_invoke(struct MsVM* vm, MsObjBuffer* b,
                                 MsObjString* method,
                                 int argc, MsValue* argv,
                                 MsValue* out);

#endif /* MS_OBJBUFFER_H */
