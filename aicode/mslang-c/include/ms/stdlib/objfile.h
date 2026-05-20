#ifndef MS_OBJFILE_H
#define MS_OBJFILE_H

#include "ms/object.h"
#include <stdio.h>

typedef enum {
    MS_FILE_TEXT   = 0,
    MS_FILE_BINARY = 1,
} MsFileMode;

struct MsObjFile {
    MsObject   obj;   /* type = MS_OBJ_FILE */
    FILE*      fp;    /* NULL when closed */
    MsFileMode mode;
    bool       eof;
};

typedef struct MsObjFile MsObjFile;

struct MsVM;
typedef struct MsObjString MsObjString;

MsObjFile* ms_obj_file_new(struct MsVM* vm, FILE* fp, MsFileMode mode);
MsObjFile* ms_obj_file_from_fd(struct MsVM* vm, int fd, const char* open_mode);
bool       ms_objfile_invoke(struct MsVM* vm, MsObjFile* f,
                              MsObjString* method,
                              int argc, MsValue* argv,
                              MsValue* out);

#endif /* MS_OBJFILE_H */
