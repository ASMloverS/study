#include "ms/stdlib/objfile.h"
#include "ms/stdlib/objbuffer.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/memory.h"
#include "ms/module.h"
#include "ms/threadpool.h"
#include "ms/common.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#  include <io.h>
#else
#  include <unistd.h>
#endif

MsObjFile* ms_obj_file_new(struct MsVM* vm, FILE* fp, MsFileMode mode) {
    MsObjFile* f = MS_ALLOC_OBJ(vm, MS_OBJ_FILE, MsObjFile, 0);
    f->fp   = fp;
    f->mode = mode;
    f->eof  = false;
    return f;
}

MsObjFile* ms_obj_file_from_fd(struct MsVM* vm, int fd, const char* open_mode) {
#ifdef _WIN32
    FILE* fp = _fdopen(fd, open_mode);
#else
    FILE* fp = fdopen(fd, open_mode);
#endif
    if (!fp) return NULL;
    MsFileMode mode = (strchr(open_mode, 'b') != NULL) ? MS_FILE_BINARY : MS_FILE_TEXT;
    return ms_obj_file_new(vm, fp, mode);
}

bool ms_objfile_invoke(struct MsVM* vm, MsObjFile* f,
                       MsObjString* method, int argc, MsValue* argv,
                       MsValue* out) {
    (void)argc;
    const char* name = method->data;

    if (strcmp(name, "close") == 0) {
        if (f->fp) { fclose(f->fp); f->fp = NULL; }
        *out = MS_NIL_VAL();
        return true;
    }
    if (strcmp(name, "flush") == 0) {
        if (f->fp) fflush(f->fp);
        *out = MS_NIL_VAL();
        return true;
    }
    if (strcmp(name, "eof") == 0) {
        bool is_eof = f->eof || (f->fp && feof(f->fp));
        *out = MS_BOOL_VAL(is_eof);
        return true;
    }
    if (strcmp(name, "tell") == 0) {
        long pos = f->fp ? ftell(f->fp) : -1;
        *out = MS_INT_VAL((ms_i64)pos);
        return true;
    }
    if (strcmp(name, "fd") == 0) {
        int fd = -1;
        if (f->fp) {
#ifdef _WIN32
            fd = _fileno(f->fp);
#else
            fd = fileno(f->fp);
#endif
        }
        *out = MS_INT_VAL((ms_i64)fd);
        return true;
    }
    if (strcmp(name, "seek") == 0) {
        if (f->fp && argc >= 2) {
            long off    = (long)MS_AS_INT(argv[0]);
            int  whence = (int)MS_AS_INT(argv[1]);
            fseek(f->fp, off, whence);
        }
        *out = MS_NIL_VAL();
        return true;
    }
    if (strcmp(name, "write") == 0) {
        if (f->fp && argc >= 1) {
            if (MS_IS_STRING(argv[0])) {
                MsObjString* s = MS_AS_STRING(argv[0]);
                fwrite(s->data, 1, (size_t)s->length, f->fp);
            } else if (MS_IS_BUFFER(argv[0])) {
                MsObjBuffer* b = MS_AS_BUFFER(argv[0]);
                fwrite(b->data, 1, b->len, f->fp);
            }
        }
        *out = MS_NIL_VAL();
        return true;
    }
    if (strcmp(name, "read") == 0) {
        if (!f->fp) { *out = MS_NIL_VAL(); return true; }
        long n = (argc >= 1) ? (long)MS_AS_INT(argv[0]) : -1;
        if (n < 0) {
            /* read to EOF */
            long start = ftell(f->fp);
            fseek(f->fp, 0, SEEK_END);
            long end = ftell(f->fp);
            fseek(f->fp, start, SEEK_SET);
            size_t size = (size_t)(end - start);
            if (size == 0) { f->eof = true; *out = MS_NIL_VAL(); return true; }
            char* buf = (char*)ms_reallocate(vm, NULL, 0, size + 1);
            size_t rd = fread(buf, 1, size, f->fp);
            buf[rd] = '\0';
            /* Force EOF flag: peek one byte to trigger feof */
            { int c = fgetc(f->fp); if (c == EOF) f->eof = true; else ungetc(c, f->fp); }
            if (f->mode == MS_FILE_TEXT) {
                *out = MS_OBJ_VAL(ms_obj_string_take(vm, buf, (int)rd));
            } else {
                MsObjBuffer* b = ms_obj_buffer_from_bytes(vm, (uint8_t*)buf, rd);
                ms_reallocate(vm, buf, rd + 1, 0);
                *out = MS_OBJ_VAL(b);
            }
        } else {
            char* buf = (char*)ms_reallocate(vm, NULL, 0, (size_t)n + 1);
            size_t rd = fread(buf, 1, (size_t)n, f->fp);
            buf[rd] = '\0';
            if (feof(f->fp)) f->eof = true;
            if (rd == 0) { ms_reallocate(vm, buf, (size_t)n + 1, 0); *out = MS_NIL_VAL(); return true; }
            if (f->mode == MS_FILE_TEXT) {
                *out = MS_OBJ_VAL(ms_obj_string_take(vm, buf, (int)rd));
            } else {
                MsObjBuffer* b = ms_obj_buffer_from_bytes(vm, (uint8_t*)buf, rd);
                ms_reallocate(vm, buf, (size_t)n + 1, 0);
                *out = MS_OBJ_VAL(b);
            }
        }
        return true;
    }
    if (strcmp(name, "readline") == 0) {
        if (!f->fp) { *out = MS_NIL_VAL(); return true; }
        char chunk[256];
        char* line = NULL;
        size_t total = 0;
        size_t cap   = 0;
        while (fgets(chunk, sizeof(chunk), f->fp)) {
            size_t len = strlen(chunk);
            if (total + len + 1 > cap) {
                size_t new_cap = cap < 256 ? 256 : cap * 2;
                while (new_cap < total + len + 1) new_cap *= 2;
                line = (char*)ms_reallocate(vm, line, cap, new_cap);
                cap = new_cap;
            }
            memcpy(line + total, chunk, len);
            total += len;
            if (chunk[len - 1] == '\n') break;
        }
        if (total == 0) {
            if (line) ms_reallocate(vm, line, cap, 0);
            if (feof(f->fp)) f->eof = true;
            *out = MS_NIL_VAL();
            return true;
        }
        line[total] = '\0';
        *out = MS_OBJ_VAL(ms_obj_string_take(vm, line, (int)total));
        return true;
    }
    return false;
}

/* ---- Async file IO (CAPI-07) ---- */

static MsValue ms_io_read_file_async(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "read_file_async: expected string path");
        return MS_NIL_VAL();
    }
    const char* path = MS_AS_CSTRING(argv[0]);
    MsObjFuture* fut = ms_obj_future_new(vm);
    ms_vm_pin_future(vm, fut);

    MsJob* job = (MsJob*)calloc(1, sizeof(MsJob));
    job->kind          = MS_JOB_READ_FILE;
    job->path          = ms_strdup(path);
    job->future_opaque = fut;
    ms_threadpool_submit(&vm->threadpool, job);
    return MS_OBJ_VAL((MsObject*)fut);
}

static MsValue ms_io_write_file_async(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2 || !MS_IS_STRING(argv[0]) || !MS_IS_STRING(argv[1])) {
        ms_vm_runtime_error(vm, "write_file_async: expected path and content strings");
        return MS_NIL_VAL();
    }
    const char* path = MS_AS_CSTRING(argv[0]);
    MsObjString* content = MS_AS_STRING(argv[1]);
    MsObjFuture* fut = ms_obj_future_new(vm);
    ms_vm_pin_future(vm, fut);

    MsJob* job = (MsJob*)calloc(1, sizeof(MsJob));
    job->kind      = MS_JOB_WRITE_FILE;
    job->path      = ms_strdup(path);
    job->write_len = (size_t)content->length;
    if (content->length > 0) {
        job->write_buf = (char*)malloc(job->write_len);
        if (!job->write_buf) {
            ms_vm_unpin_future(vm, fut);
            free(job->path);
            free(job);
            ms_vm_runtime_error(vm, "write_file_async: out of memory");
            return MS_NIL_VAL();
        }
        memcpy(job->write_buf, content->data, job->write_len);
    }
    job->future_opaque = fut;
    ms_threadpool_submit(&vm->threadpool, job);
    return MS_OBJ_VAL((MsObject*)fut);
}

static const MsNativeDef io_defs[] = {
    { "read_file_async",  ms_io_read_file_async,  1 },
    { "write_file_async", ms_io_write_file_async, 2 },
    { NULL, NULL, 0 }
};

void ms_module_io_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, io_defs);
}
