/* src/stdlib/bufio.c - STDLIB-30: bufio module
 *
 * Buffered IO on top of io.File + ObjBuffer.
 *
 * Public API (module-level):
 *   bufio.new_reader(file, buf_size=4096)  → BufReader userdata
 *   bufio.new_writer(file, buf_size=4096)  → BufWriter userdata
 *   bufio.read_lines(filepath)             → list[str]
 *   bufio.write_lines(filepath, lines)     → nil
 *
 * BufReader methods (OOP dispatch via ms_builtin_invoke):
 *   br.read_line()         → str|nil
 *   br.read_lines()        → list[str]
 *   br.read_chunk(n)       → buffer
 *   br.read_until(delim)   → buffer
 *   br.read_all()          → buffer
 *   br.peek(n)             → buffer
 *
 * BufWriter methods:
 *   bw.write(data)         → nil  (str or buffer)
 *   bw.write_line(s)       → nil
 *   bw.flush()             → nil
 */

#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/value.h"
#include "ms/memory.h"
#include "ms/stdlib/objfile.h"
#include "ms/stdlib/objbuffer.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define BUFRD_TAG "bufio.BufReader"
#define BUFWR_TAG "bufio.BufWriter"

/* ------------------------------------------------------------------ */
/* BufReader internal state                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    MsObjFile* file;
    uint8_t*   buf;
    size_t     buf_cap;
    size_t     pos;     /* read cursor */
    size_t     filled;  /* valid bytes */
    bool       eof;
} BufReader;

static void bufrd_finalize(void* data) {
    BufReader* rd = (BufReader*)data;
    free(rd->buf);
}

static void bufrd_mark(MsVM* vm, void* data) {
    BufReader* rd = (BufReader*)data;
    if (rd->file) ms_mark_object(vm, (MsObject*)rd->file);
}

/* ------------------------------------------------------------------ */
/* BufWriter internal state                                             */
/* ------------------------------------------------------------------ */

typedef struct {
    MsObjFile* file;
    uint8_t*   buf;
    size_t     buf_cap;
    size_t     pos;     /* pending bytes */
} BufWriter;

static void bufwr_finalize(void* data) {
    BufWriter* wr = (BufWriter*)data;
    /* best-effort flush on GC */
    if (wr->file && wr->file->fp && wr->pos > 0) {
        (void)fwrite(wr->buf, 1, wr->pos, wr->file->fp);
    }
    free(wr->buf);
}

static void bufwr_mark(MsVM* vm, void* data) {
    BufWriter* wr = (BufWriter*)data;
    if (wr->file) ms_mark_object(vm, (MsObject*)wr->file);
}

/* ------------------------------------------------------------------ */
/* BufReader: fill buffer from file                                     */
/* ------------------------------------------------------------------ */

static bool bufrd_refill(BufReader* rd) {
    if (rd->eof) return false;
    if (!rd->file || !rd->file->fp) { rd->eof = true; return false; }
    size_t remain = rd->filled - rd->pos;
    if (remain > 0 && rd->pos > 0) {
        memmove(rd->buf, rd->buf + rd->pos, remain);
    }
    rd->pos    = 0;
    rd->filled = remain;
    size_t room = rd->buf_cap - rd->filled;
    if (room == 0) return rd->filled > 0;
    size_t n = fread(rd->buf + rd->filled, 1, room, rd->file->fp);
    rd->filled += n;
    if (n == 0) { rd->eof = true; rd->file->eof = true; }
    return rd->filled > rd->pos;
}

/* ------------------------------------------------------------------ */
/* bufio.new_reader(file, buf_size=4096)                                */
/* ------------------------------------------------------------------ */

static MsValue bufio_new_reader(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_FILE(argv[0])) {
        ms_vm_runtime_error(vm, "bufio.new_reader: expected File as first arg");
        return MS_NIL_VAL();
    }
    size_t buf_size = 4096;
    if (argc >= 2) {
        if (!MS_IS_INT(argv[1]) || MS_AS_INT(argv[1]) <= 0) {
            ms_vm_runtime_error(vm, "bufio.new_reader: buf_size must be positive int");
            return MS_NIL_VAL();
        }
        buf_size = (size_t)MS_AS_INT(argv[1]);
    }
    MsObjUserdata* ud = ms_obj_userdata_new(vm, sizeof(BufReader),
                                             bufrd_finalize, bufrd_mark, BUFRD_TAG);
    BufReader* rd = (BufReader*)ud->data;
    rd->file    = MS_AS_FILE(argv[0]);
    rd->buf_cap = buf_size;
    rd->buf     = (uint8_t*)malloc(buf_size);
    rd->pos     = 0;
    rd->filled  = 0;
    rd->eof     = false;
    if (!rd->buf) {
        ms_vm_runtime_error(vm, "bufio.new_reader: OOM");
        return MS_NIL_VAL();
    }
    return MS_OBJ_VAL(ud);
}

/* ------------------------------------------------------------------ */
/* BufReader.read_line() → str|nil                                      */
/* ------------------------------------------------------------------ */

static MsValue bufrd_read_line(MsVM* vm, BufReader* rd) {
    size_t out_cap = 128;
    char*  out     = (char*)malloc(out_cap);
    size_t out_len = 0;
    if (!out) { ms_vm_runtime_error(vm, "read_line: OOM"); return MS_NIL_VAL(); }

    bool found = false;
    while (!found) {
        if (rd->pos >= rd->filled) {
            if (!bufrd_refill(rd)) break;
        }
        const uint8_t* start = rd->buf + rd->pos;
        size_t avail = rd->filled - rd->pos;
        const uint8_t* nl = (const uint8_t*)memchr(start, '\n', avail);
        size_t take = nl ? (size_t)(nl - start) : avail;
        while (out_len + take + 2 > out_cap) {
            out_cap *= 2;
            char* nb = (char*)realloc(out, out_cap);
            if (!nb) { free(out); ms_vm_runtime_error(vm, "read_line: OOM"); return MS_NIL_VAL(); }
            out = nb;
        }
        memcpy(out + out_len, start, take);
        out_len += take;
        rd->pos += take;
        if (nl) { rd->pos++; found = true; } /* consume '\n' */
    }

    if (out_len == 0 && !found) { free(out); return MS_NIL_VAL(); }
    if (out_len > 0 && out[out_len - 1] == '\r') out_len--; /* strip CR */
    out[out_len] = '\0';
    MsValue r = MS_OBJ_VAL(ms_obj_string_copy(vm, out, (int)out_len));
    free(out);
    return r;
}

/* ------------------------------------------------------------------ */
/* BufReader.read_chunk(n) → buffer                                     */
/* ------------------------------------------------------------------ */

static MsValue bufrd_read_chunk(MsVM* vm, BufReader* rd, size_t want) {
    MsObjBuffer* out = ms_obj_buffer_new(vm, want > 0 ? want : 1);
    size_t got = 0;
    while (got < want) {
        if (rd->pos >= rd->filled && !bufrd_refill(rd)) break;
        size_t avail = rd->filled - rd->pos;
        size_t take  = (want - got) < avail ? (want - got) : avail;
        if (got + take > out->cap) {
            size_t nc = got + take;
            uint8_t* nb = (uint8_t*)realloc(out->data, nc);
            if (!nb) { ms_vm_runtime_error(vm, "read_chunk: OOM"); return MS_NIL_VAL(); }
            out->data = nb; out->cap = nc;
        }
        memcpy(out->data + got, rd->buf + rd->pos, take);
        rd->pos += take; got += take;
    }
    out->len = got;
    return MS_OBJ_VAL(out);
}

/* ------------------------------------------------------------------ */
/* BufReader.read_until(delim) → buffer  (delim included)              */
/* ------------------------------------------------------------------ */

static MsValue bufrd_read_until(MsVM* vm, BufReader* rd, uint8_t delim) {
    size_t out_cap = 128;
    uint8_t* out   = (uint8_t*)malloc(out_cap);
    size_t out_len = 0;
    if (!out) { ms_vm_runtime_error(vm, "read_until: OOM"); return MS_NIL_VAL(); }
    bool done = false;
    while (!done) {
        if (rd->pos >= rd->filled && !bufrd_refill(rd)) break;
        const uint8_t* start = rd->buf + rd->pos;
        size_t avail = rd->filled - rd->pos;
        const uint8_t* hit = (const uint8_t*)memchr(start, delim, avail);
        size_t take = hit ? (size_t)(hit - start + 1) : avail;
        while (out_len + take + 1 > out_cap) {
            out_cap *= 2;
            uint8_t* nb = (uint8_t*)realloc(out, out_cap);
            if (!nb) { free(out); ms_vm_runtime_error(vm, "read_until: OOM"); return MS_NIL_VAL(); }
            out = nb;
        }
        memcpy(out + out_len, start, take);
        out_len += take; rd->pos += take;
        if (hit) done = true;
    }
    MsObjBuffer* b = ms_obj_buffer_from_bytes(vm, out, out_len);
    free(out);
    return MS_OBJ_VAL(b);
}

/* ------------------------------------------------------------------ */
/* BufReader.read_all() → buffer                                        */
/* ------------------------------------------------------------------ */

static MsValue bufrd_read_all(MsVM* vm, BufReader* rd) {
    size_t out_cap = rd->buf_cap > 0 ? rd->buf_cap : 1;
    uint8_t* out   = (uint8_t*)malloc(out_cap);
    size_t out_len = 0;
    if (!out) { ms_vm_runtime_error(vm, "read_all: OOM"); return MS_NIL_VAL(); }
    while (true) {
        if (rd->pos >= rd->filled && !bufrd_refill(rd)) break;
        size_t avail = rd->filled - rd->pos;
        while (out_len + avail + 1 > out_cap) {
            out_cap *= 2;
            uint8_t* nb = (uint8_t*)realloc(out, out_cap);
            if (!nb) { free(out); ms_vm_runtime_error(vm, "read_all: OOM"); return MS_NIL_VAL(); }
            out = nb;
        }
        memcpy(out + out_len, rd->buf + rd->pos, avail);
        out_len += avail; rd->pos += avail;
    }
    MsObjBuffer* b = ms_obj_buffer_from_bytes(vm, out, out_len);
    free(out);
    return MS_OBJ_VAL(b);
}

/* ------------------------------------------------------------------ */
/* BufReader.peek(n) → buffer  (no position advance)                   */
/* ------------------------------------------------------------------ */

static MsValue bufrd_peek(MsVM* vm, BufReader* rd, size_t want) {
    if (rd->filled - rd->pos < want) bufrd_refill(rd);
    size_t avail = rd->filled - rd->pos;
    size_t take  = want < avail ? want : avail;
    return MS_OBJ_VAL(ms_obj_buffer_from_bytes(vm, rd->buf + rd->pos, take));
}

/* ------------------------------------------------------------------ */
/* bufio.new_writer(file, buf_size=4096)                                */
/* ------------------------------------------------------------------ */

static MsValue bufio_new_writer(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_FILE(argv[0])) {
        ms_vm_runtime_error(vm, "bufio.new_writer: expected File as first arg");
        return MS_NIL_VAL();
    }
    size_t buf_size = 4096;
    if (argc >= 2) {
        if (!MS_IS_INT(argv[1]) || MS_AS_INT(argv[1]) <= 0) {
            ms_vm_runtime_error(vm, "bufio.new_writer: buf_size must be positive int");
            return MS_NIL_VAL();
        }
        buf_size = (size_t)MS_AS_INT(argv[1]);
    }
    MsObjUserdata* ud = ms_obj_userdata_new(vm, sizeof(BufWriter),
                                             bufwr_finalize, bufwr_mark, BUFWR_TAG);
    BufWriter* wr = (BufWriter*)ud->data;
    wr->file    = MS_AS_FILE(argv[0]);
    wr->buf_cap = buf_size;
    wr->buf     = (uint8_t*)malloc(buf_size);
    wr->pos     = 0;
    if (!wr->buf) {
        ms_vm_runtime_error(vm, "bufio.new_writer: OOM");
        return MS_NIL_VAL();
    }
    return MS_OBJ_VAL(ud);
}

/* ------------------------------------------------------------------ */
/* BufWriter helpers                                                     */
/* ------------------------------------------------------------------ */

static bool bufwr_do_flush(MsVM* vm, BufWriter* wr) {
    if (!wr->file || !wr->file->fp) {
        ms_vm_runtime_error(vm, "flush: file is closed");
        return false;
    }
    if (wr->pos == 0) return true;
    size_t written = fwrite(wr->buf, 1, wr->pos, wr->file->fp);
    wr->pos = 0;
    if (written == 0) { ms_vm_runtime_error(vm, "flush: write error"); return false; }
    return true;
}

static bool bufwr_write_bytes(MsVM* vm, BufWriter* wr,
                               const uint8_t* data, size_t data_len) {
    size_t offset = 0;
    while (offset < data_len) {
        size_t room = wr->buf_cap - wr->pos;
        size_t take = (data_len - offset) < room ? (data_len - offset) : room;
        memcpy(wr->buf + wr->pos, data + offset, take);
        wr->pos += take; offset += take;
        if (wr->pos >= wr->buf_cap && !bufwr_do_flush(vm, wr)) return false;
    }
    return true;
}

/* ------------------------------------------------------------------ */
/* bufio.read_lines(filepath) → list[str]                               */
/* ------------------------------------------------------------------ */

static MsValue bufio_read_lines(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "bufio.read_lines: expected filepath string");
        return MS_NIL_VAL();
    }
    const char* path = MS_AS_CSTRING(argv[0]);
    FILE* fp;
#ifdef _MSC_VER
    if (fopen_s(&fp, path, "r") != 0) fp = NULL;
#else
    fp = fopen(path, "r");
#endif
    if (!fp) { ms_vm_runtime_error(vm, "bufio.read_lines: cannot open file"); return MS_NIL_VAL(); }

    MsObjList* lst = ms_obj_list_new(vm);
    size_t cap = 256;
    char* line = (char*)malloc(cap);
    if (!line) { fclose(fp); ms_vm_runtime_error(vm, "bufio.read_lines: OOM"); return MS_NIL_VAL(); }
    size_t len = 0;
    int c;
    while ((c = fgetc(fp)) != EOF) {
        if ((size_t)(len + 2) > cap) {
            cap *= 2;
            char* nb = (char*)realloc(line, cap);
            if (!nb) { free(line); fclose(fp); ms_vm_runtime_error(vm, "bufio.read_lines: OOM"); return MS_NIL_VAL(); }
            line = nb;
        }
        if (c == '\n') {
            if (len > 0 && line[len - 1] == '\r') len--;
            line[len] = '\0';
            ms_value_array_push(&lst->items, MS_OBJ_VAL(ms_obj_string_copy(vm, line, (int)len)));
            len = 0;
        } else {
            line[len++] = (char)c;
        }
    }
    if (len > 0) {
        if (line[len - 1] == '\r') len--;
        line[len] = '\0';
        ms_value_array_push(&lst->items, MS_OBJ_VAL(ms_obj_string_copy(vm, line, (int)len)));
    }
    free(line);
    fclose(fp);
    return MS_OBJ_VAL(lst);
}

/* ------------------------------------------------------------------ */
/* bufio.write_lines(filepath, lines) → nil                             */
/* ------------------------------------------------------------------ */

static MsValue bufio_write_lines(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "bufio.write_lines: expected filepath string");
        return MS_NIL_VAL();
    }
    if (!MS_IS_LIST(argv[1])) {
        ms_vm_runtime_error(vm, "bufio.write_lines: expected list as second arg");
        return MS_NIL_VAL();
    }
    const char* path = MS_AS_CSTRING(argv[0]);
    FILE* fp;
#ifdef _MSC_VER
    if (fopen_s(&fp, path, "w") != 0) fp = NULL;
#else
    fp = fopen(path, "w");
#endif
    if (!fp) { ms_vm_runtime_error(vm, "bufio.write_lines: cannot open file"); return MS_NIL_VAL(); }

    MsObjList* lst = MS_AS_LIST(argv[1]);
    for (int i = 0; i < lst->items.count; i++) {
        MsValue v = lst->items.data[i];
        if (!MS_IS_STRING(v)) {
            fclose(fp);
            ms_vm_runtime_error(vm, "bufio.write_lines: list elements must be strings");
            return MS_NIL_VAL();
        }
        const char* s    = MS_AS_CSTRING(v);
        size_t      slen = (size_t)MS_AS_STRING(v)->length;
        fwrite(s, 1, slen, fp);
        fputc('\n', fp);
    }
    fclose(fp);
    return MS_NIL_VAL();
}

/* ================================================================== */
/* OOP method dispatch (called from vm_builtins.c)                      */
/* ================================================================== */

bool ms_bufio_invoke(MsVM* vm, MsObjUserdata* ud,
                     MsObjString* method, int argc, MsValue* argv,
                     MsValue* out) {
    const char* tag = ud->type_tag;
    const char* m   = method->data;

    /* ---- BufReader ---- */
    if (strcmp(tag, BUFRD_TAG) == 0) {
        BufReader* rd = (BufReader*)ud->data;
        if (strcmp(m, "read_line")  == 0) { *out = bufrd_read_line(vm, rd);  return true; }
        if (strcmp(m, "read_lines") == 0) {
            MsObjList* lst = ms_obj_list_new(vm);
            while (true) {
                MsValue line = bufrd_read_line(vm, rd);
                if (MS_IS_NIL(line)) break;
                ms_value_array_push(&lst->items, line);
            }
            *out = MS_OBJ_VAL(lst);
            return true;
        }
        if (strcmp(m, "read_all") == 0) { *out = bufrd_read_all(vm, rd); return true; }
        if (strcmp(m, "read_chunk") == 0) {
            if (argc < 1 || !MS_IS_INT(argv[0]) || MS_AS_INT(argv[0]) < 0) {
                ms_vm_runtime_error(vm, "read_chunk: n must be non-negative int");
                *out = MS_NIL_VAL(); return true;
            }
            *out = bufrd_read_chunk(vm, rd, (size_t)MS_AS_INT(argv[0]));
            return true;
        }
        if (strcmp(m, "read_until") == 0) {
            if (argc < 1 || !MS_IS_STRING(argv[0]) || MS_AS_STRING(argv[0])->length == 0) {
                ms_vm_runtime_error(vm, "read_until: delim must be a non-empty string");
                *out = MS_NIL_VAL(); return true;
            }
            *out = bufrd_read_until(vm, rd, (uint8_t)MS_AS_CSTRING(argv[0])[0]);
            return true;
        }
        if (strcmp(m, "peek") == 0) {
            if (argc < 1 || !MS_IS_INT(argv[0]) || MS_AS_INT(argv[0]) < 0) {
                ms_vm_runtime_error(vm, "peek: n must be non-negative int");
                *out = MS_NIL_VAL(); return true;
            }
            *out = bufrd_peek(vm, rd, (size_t)MS_AS_INT(argv[0]));
            return true;
        }
        return false;
    }

    /* ---- BufWriter ---- */
    if (strcmp(tag, BUFWR_TAG) == 0) {
        BufWriter* wr = (BufWriter*)ud->data;
        if (strcmp(m, "flush") == 0) {
            bufwr_do_flush(vm, wr);
            *out = MS_NIL_VAL(); return true;
        }
        if (strcmp(m, "write") == 0) {
            if (argc < 1) {
                ms_vm_runtime_error(vm, "write: expected string or buffer argument");
                *out = MS_NIL_VAL(); return true;
            }
            const uint8_t* data; size_t data_len;
            if (MS_IS_STRING(argv[0])) {
                data     = (const uint8_t*)MS_AS_CSTRING(argv[0]);
                data_len = (size_t)MS_AS_STRING(argv[0])->length;
            } else if (MS_IS_BUFFER(argv[0])) {
                MsObjBuffer* b = MS_AS_BUFFER(argv[0]);
                data = b->data; data_len = b->len;
            } else {
                ms_vm_runtime_error(vm, "write: expected string or buffer");
                *out = MS_NIL_VAL(); return true;
            }
            bufwr_write_bytes(vm, wr, data, data_len);
            *out = MS_NIL_VAL(); return true;
        }
        if (strcmp(m, "write_line") == 0) {
            if (argc < 1 || !MS_IS_STRING(argv[0])) {
                ms_vm_runtime_error(vm, "write_line: expected string");
                *out = MS_NIL_VAL(); return true;
            }
            const uint8_t* data = (const uint8_t*)MS_AS_CSTRING(argv[0]);
            size_t data_len = (size_t)MS_AS_STRING(argv[0])->length;
            if (!bufwr_write_bytes(vm, wr, data, data_len)) { *out = MS_NIL_VAL(); return true; }
            static const uint8_t nl = '\n';
            bufwr_write_bytes(vm, wr, &nl, 1);
            *out = MS_NIL_VAL(); return true;
        }
        return false;
    }

    return false;
}

/* ================================================================== */
/* Module init                                                          */
/* ================================================================== */

void ms_module_bufio_init(MsVM* vm, MsObjModule* mod) {
    static const MsNativeDef defs[] = {
        {"new_reader",  bufio_new_reader,  -1},
        {"new_writer",  bufio_new_writer,  -1},
        {"read_lines",  bufio_read_lines,   1},
        {"write_lines", bufio_write_lines,  2},
        {NULL, NULL, 0}
    };
    ms_module_register_natives(vm, mod, defs);
}
