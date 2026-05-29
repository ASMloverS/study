/* src/stdlib/csv.c - STDLIB-33: csv module
 *
 * CSV read/write: custom separator, quote escaping, header-dict mode.
 * Corresponds to Go's encoding/csv.
 *
 * Public API (module-level):
 *   csv.read_str(s, sep=',', quote='"')  → list[list]
 *   csv.read_file(path, sep=',')         → list[list]
 *   csv.read_dict(path, sep=',')         → list[map]
 *   csv.write_str(rows, sep=',')         → str
 *   csv.write_file(path, rows, sep=',')  → nil
 *   csv.reader(file, sep=',')            → CsvReader userdata
 *   csv.writer(file, sep=',')            → CsvWriter userdata
 *
 * CsvReader methods:
 *   cr.read_row()   → list|nil
 *   cr.read_all()   → list[list]
 *
 * CsvWriter methods:
 *   cw.write_row(row)    → nil
 *   cw.write_all(rows)   → nil
 */

#include "ms/module.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/value.h"
#include "ms/memory.h"
#include "ms/stdlib/objfile.h"
#include "ms/vtable.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CSV_READER_TAG "csv.CsvReader"
#define CSV_WRITER_TAG "csv.CsvWriter"

/* ------------------------------------------------------------------ */
/* CSV parsing helpers                                                  */
/* ------------------------------------------------------------------ */

/* Parse a single CSV row from buf[pos..len].
 * Updates *pos past the consumed characters (including trailing '\n').
 * sep and quote are single chars.
 * Returns a newly allocated MsObjList* or NULL on empty input / EOF. */
static MsObjList* csv_parse_row(MsVM* vm,
                                  const char* buf, size_t len, size_t* pos,
                                  char sep, char quote)
{
    if (*pos >= len) return NULL;

    /* Skip a single leading '\n' that was left by the previous row */
    /* (we consume '\n' at end of each row, so nothing to skip here) */

    MsObjList* row = ms_obj_list_new(vm);

    /* field accumulator */
    size_t field_cap = 64;
    char* field = (char*)malloc(field_cap);
    if (!field) { ms_vm_runtime_error(vm, "csv: OOM"); return NULL; }
    size_t field_len = 0;

    bool in_row = false;

    while (*pos <= len) {
        /* End of input */
        if (*pos == len) {
            /* flush last field if we were in a row */
            if (in_row || row->items.count > 0) {
                MsValue fv = MS_OBJ_VAL(ms_obj_string_copy(vm, field, (int)field_len));
                ms_value_array_push(&row->items, fv);
            }
            break;
        }

        char c = buf[*pos];

        /* Newline: end of row */
        if (c == '\r') {
            (*pos)++;
            /* consume following '\n' if present */
            if (*pos < len && buf[*pos] == '\n') (*pos)++;
            MsValue fv = MS_OBJ_VAL(ms_obj_string_copy(vm, field, (int)field_len));
            ms_value_array_push(&row->items, fv);
            break;
        }
        if (c == '\n') {
            (*pos)++;
            MsValue fv = MS_OBJ_VAL(ms_obj_string_copy(vm, field, (int)field_len));
            ms_value_array_push(&row->items, fv);
            break;
        }

        in_row = true;

        /* Quoted field */
        if (c == quote) {
            (*pos)++; /* skip opening quote */
            while (*pos < len) {
                char qc = buf[*pos];
                if (qc == quote) {
                    (*pos)++;
                    /* doubled quote → literal quote */
                    if (*pos < len && buf[*pos] == quote) {
                        if (field_len + 1 >= field_cap) {
                            field_cap *= 2;
                            char* nb = (char*)realloc(field, field_cap);
                            if (!nb) { free(field); ms_vm_runtime_error(vm, "csv: OOM"); return NULL; }
                            field = nb;
                        }
                        field[field_len++] = quote;
                        (*pos)++;
                    } else {
                        break; /* closing quote */
                    }
                } else {
                    if (field_len + 1 >= field_cap) {
                        field_cap *= 2;
                        char* nb = (char*)realloc(field, field_cap);
                        if (!nb) { free(field); ms_vm_runtime_error(vm, "csv: OOM"); return NULL; }
                        field = nb;
                    }
                    field[field_len++] = qc;
                    (*pos)++;
                }
            }
            /* after closing quote: expect sep, newline, or EOF */
            continue;
        }

        /* Separator → end of field */
        if (c == sep) {
            (*pos)++;
            MsValue fv = MS_OBJ_VAL(ms_obj_string_copy(vm, field, (int)field_len));
            ms_value_array_push(&row->items, fv);
            field_len = 0;
            continue;
        }

        /* Normal character */
        if (field_len + 1 >= field_cap) {
            field_cap *= 2;
            char* nb = (char*)realloc(field, field_cap);
            if (!nb) { free(field); ms_vm_runtime_error(vm, "csv: OOM"); return NULL; }
            field = nb;
        }
        field[field_len++] = c;
        (*pos)++;
    }

    free(field);

    /* Empty row (blank line) → return empty list still */
    return row;
}

/* Parse all rows from a NUL-terminated string. */
static MsObjList* csv_parse_all(MsVM* vm,
                                  const char* src, size_t src_len,
                                  char sep, char quote)
{
    MsObjList* result = ms_obj_list_new(vm);
    size_t pos = 0;
    while (pos < src_len) {
        MsObjList* row = csv_parse_row(vm, src, src_len, &pos, sep, quote);
        if (!row) break;
        /* skip completely empty rows (blank lines) */
        if (row->items.count > 0 || pos < src_len) {
            if (row->items.count > 0)
                ms_value_array_push(&result->items, MS_OBJ_VAL(row));
        }
    }
    return result;
}

/* ------------------------------------------------------------------ */
/* CSV writing helpers                                                  */
/* ------------------------------------------------------------------ */

/* Append a quoted/unquoted field to a grow-buffer. */
static bool csv_write_field(char** buf, size_t* buf_len, size_t* buf_cap,
                              const char* field, int flen,
                              char sep, char quote)
{
    /* Determine if quoting needed */
    bool needs_quote = false;
    for (int i = 0; i < flen; i++) {
        char c = field[i];
        if (c == sep || c == quote || c == '\n' || c == '\r') {
            needs_quote = true;
            break;
        }
    }

    /* Worst case: each char doubled + 2 quotes */
    size_t need = (size_t)(flen * 2 + 3);
    while (*buf_len + need >= *buf_cap) {
        *buf_cap = (*buf_cap < 64) ? 128 : (*buf_cap) * 2;
        char* nb = (char*)realloc(*buf, *buf_cap);
        if (!nb) return false;
        *buf = nb;
    }

    if (needs_quote) {
        (*buf)[(*buf_len)++] = quote;
        for (int i = 0; i < flen; i++) {
            char c = field[i];
            if (c == quote) (*buf)[(*buf_len)++] = quote; /* double it */
            (*buf)[(*buf_len)++] = c;
        }
        (*buf)[(*buf_len)++] = quote;
    } else {
        memcpy(*buf + *buf_len, field, (size_t)flen);
        *buf_len += (size_t)flen;
    }
    return true;
}

/* Serialize a list[list] to a CSV string (in a malloc'd buffer). */
static char* csv_serialize_rows(MsVM* vm, MsObjList* rows,
                                  char sep, size_t* out_len)
{
    char quote = '"';
    size_t buf_cap = 256;
    size_t buf_len = 0;
    char* buf = (char*)malloc(buf_cap);
    if (!buf) { ms_vm_runtime_error(vm, "csv.write_str: OOM"); return NULL; }

    for (int i = 0; i < rows->items.count; i++) {
        MsValue rv = rows->items.data[i];
        if (!MS_IS_LIST(rv)) {
            ms_vm_runtime_error(vm, "csv.write_str: each row must be a list");
            free(buf);
            return NULL;
        }
        MsObjList* row = MS_AS_LIST(rv);
        for (int j = 0; j < row->items.count; j++) {
            if (j > 0) {
                /* ensure room for separator */
                if (buf_len + 2 >= buf_cap) {
                    buf_cap *= 2;
                    char* nb = (char*)realloc(buf, buf_cap);
                    if (!nb) { free(buf); ms_vm_runtime_error(vm, "csv.write_str: OOM"); return NULL; }
                    buf = nb;
                }
                buf[buf_len++] = sep;
            }
            MsValue fv = row->items.data[j];
            /* stringify non-strings */
            char* tmp_free = NULL;
            const char* field;
            int flen;
            if (MS_IS_STRING(fv)) {
                field = MS_AS_CSTRING(fv);
                flen  = MS_AS_STRING(fv)->length;
            } else {
                tmp_free = ms_value_to_cstring(fv);
                field    = tmp_free ? tmp_free : "";
                flen     = (int)strlen(field);
            }
            bool ok = csv_write_field(&buf, &buf_len, &buf_cap,
                                       field, flen, sep, quote);
            free(tmp_free);
            if (!ok) { free(buf); ms_vm_runtime_error(vm, "csv.write_str: OOM"); return NULL; }
        }
        /* newline at end of row */
        if (buf_len + 2 >= buf_cap) {
            buf_cap *= 2;
            char* nb = (char*)realloc(buf, buf_cap);
            if (!nb) { free(buf); ms_vm_runtime_error(vm, "csv.write_str: OOM"); return NULL; }
            buf = nb;
        }
        buf[buf_len++] = '\n';
    }
    buf[buf_len] = '\0';
    *out_len = buf_len;
    return buf;
}

/* ------------------------------------------------------------------ */
/* csv.read_str(s, sep=',', quote='"') → list[list]                   */
/* ------------------------------------------------------------------ */

static MsValue csv_read_str(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "csv.read_str: expected string");
        return MS_NIL_VAL();
    }
    char sep   = ',';
    char quote = '"';
    if (argc >= 2 && MS_IS_STRING(argv[1]) && MS_AS_STRING(argv[1])->length > 0)
        sep = MS_AS_CSTRING(argv[1])[0];
    if (argc >= 3 && MS_IS_STRING(argv[2]) && MS_AS_STRING(argv[2])->length > 0)
        quote = MS_AS_CSTRING(argv[2])[0];

    const char* src = MS_AS_CSTRING(argv[0]);
    size_t src_len  = (size_t)MS_AS_STRING(argv[0])->length;
    return MS_OBJ_VAL(csv_parse_all(vm, src, src_len, sep, quote));
}

/* ------------------------------------------------------------------ */
/* Helper: read entire file into a malloc'd buffer                     */
/* ------------------------------------------------------------------ */

static char* file_read_all(const char* path, size_t* out_len) {
    FILE* fp = NULL;
#ifdef _MSC_VER
    fopen_s(&fp, path, "rb");
#else
    fp = fopen(path, "rb");
#endif
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    rewind(fp);
    if (sz < 0) { fclose(fp); return NULL; }
    char* buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(fp); return NULL; }
    size_t n = fread(buf, 1, (size_t)sz, fp);
    fclose(fp);
    buf[n] = '\0';
    if (out_len) *out_len = n;
    return buf;
}

/* ------------------------------------------------------------------ */
/* csv.read_file(path, sep=',') → list[list]                          */
/* ------------------------------------------------------------------ */

static MsValue csv_read_file(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "csv.read_file: expected path string");
        return MS_NIL_VAL();
    }
    char sep = ',';
    if (argc >= 2 && MS_IS_STRING(argv[1]) && MS_AS_STRING(argv[1])->length > 0)
        sep = MS_AS_CSTRING(argv[1])[0];

    size_t src_len = 0;
    char* src = file_read_all(MS_AS_CSTRING(argv[0]), &src_len);
    if (!src) {
        ms_vm_runtime_error(vm, "csv.read_file: cannot open file");
        return MS_NIL_VAL();
    }
    MsObjList* result = csv_parse_all(vm, src, src_len, sep, '"');
    free(src);
    return MS_OBJ_VAL(result);
}

/* ------------------------------------------------------------------ */
/* csv.read_dict(path, sep=',') → list[map]                           */
/* ------------------------------------------------------------------ */

static MsValue csv_read_dict(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "csv.read_dict: expected path string");
        return MS_NIL_VAL();
    }
    char sep = ',';
    if (argc >= 2 && MS_IS_STRING(argv[1]) && MS_AS_STRING(argv[1])->length > 0)
        sep = MS_AS_CSTRING(argv[1])[0];

    size_t src_len = 0;
    char* src = file_read_all(MS_AS_CSTRING(argv[0]), &src_len);
    if (!src) {
        ms_vm_runtime_error(vm, "csv.read_dict: cannot open file");
        return MS_NIL_VAL();
    }
    MsObjList* all = csv_parse_all(vm, src, src_len, sep, '"');
    free(src);

    MsObjList* result = ms_obj_list_new(vm);
    if (all->items.count == 0) return MS_OBJ_VAL(result);

    /* First row = headers */
    MsObjList* headers = MS_AS_LIST(all->items.data[0]);
    int ncols = headers->items.count;

    for (int i = 1; i < all->items.count; i++) {
        MsObjList* row = MS_AS_LIST(all->items.data[i]);
        MsObjMap* m = ms_obj_map_new(vm);
        for (int j = 0; j < ncols; j++) {
            MsValue key = (j < headers->items.count) ? headers->items.data[j] : MS_OBJ_VAL(ms_obj_string_copy(vm, "", 0));
            MsValue val = (j < row->items.count)    ? row->items.data[j]    : MS_OBJ_VAL(ms_obj_string_copy(vm, "", 0));
            ms_vtable_set(&m->table, key, val);
        }
        ms_value_array_push(&result->items, MS_OBJ_VAL(m));
    }
    return MS_OBJ_VAL(result);
}

/* ------------------------------------------------------------------ */
/* csv.write_str(rows, sep=',') → str                                  */
/* ------------------------------------------------------------------ */

static MsValue csv_write_str(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_LIST(argv[0])) {
        ms_vm_runtime_error(vm, "csv.write_str: expected list of rows");
        return MS_NIL_VAL();
    }
    char sep = ',';
    if (argc >= 2 && MS_IS_STRING(argv[1]) && MS_AS_STRING(argv[1])->length > 0)
        sep = MS_AS_CSTRING(argv[1])[0];

    size_t out_len = 0;
    char* buf = csv_serialize_rows(vm, MS_AS_LIST(argv[0]), sep, &out_len);
    if (!buf) return MS_NIL_VAL();
    MsValue r = MS_OBJ_VAL(ms_obj_string_copy(vm, buf, (int)out_len));
    free(buf);
    return r;
}

/* ------------------------------------------------------------------ */
/* csv.write_file(path, rows, sep=',') → nil                           */
/* ------------------------------------------------------------------ */

static MsValue csv_write_file(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2 || !MS_IS_STRING(argv[0]) || !MS_IS_LIST(argv[1])) {
        ms_vm_runtime_error(vm, "csv.write_file: expected (path, rows[, sep])");
        return MS_NIL_VAL();
    }
    char sep = ',';
    if (argc >= 3 && MS_IS_STRING(argv[2]) && MS_AS_STRING(argv[2])->length > 0)
        sep = MS_AS_CSTRING(argv[2])[0];

    size_t out_len = 0;
    char* buf = csv_serialize_rows(vm, MS_AS_LIST(argv[1]), sep, &out_len);
    if (!buf) return MS_NIL_VAL();

    const char* path = MS_AS_CSTRING(argv[0]);
    FILE* fp = NULL;
#ifdef _MSC_VER
    fopen_s(&fp, path, "wb");
#else
    fp = fopen(path, "wb");
#endif
    if (!fp) {
        free(buf);
        ms_vm_runtime_error(vm, "csv.write_file: cannot open file for writing");
        return MS_NIL_VAL();
    }
    fwrite(buf, 1, out_len, fp);
    fclose(fp);
    free(buf);
    return MS_NIL_VAL();
}

/* ================================================================== */
/* CsvReader / CsvWriter userdata                                       */
/* ================================================================== */

typedef struct {
    MsObjFile* file;
    char       sep;
    char       quote;
    /* line buffer */
    char*      line_buf;
    size_t     line_cap;
    size_t     line_len;
    size_t     line_pos;
    bool       eof;
} CsvReader;

typedef struct {
    MsObjFile* file;
    char       sep;
} CsvWriter;

static void csvrd_finalize(void* data) {
    CsvReader* rd = (CsvReader*)data;
    free(rd->line_buf);
}

static void csvrd_mark(MsVM* vm, void* data) {
    CsvReader* rd = (CsvReader*)data;
    if (rd->file) ms_mark_object(vm, (MsObject*)rd->file);
}

static void csvwr_mark(MsVM* vm, void* data) {
    CsvWriter* wr = (CsvWriter*)data;
    if (wr->file) ms_mark_object(vm, (MsObject*)wr->file);
}

/* ------------------------------------------------------------------ */
/* csv.reader(file, sep=',') → CsvReader                              */
/* ------------------------------------------------------------------ */

static MsValue csv_reader(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_FILE(argv[0])) {
        ms_vm_runtime_error(vm, "csv.reader: expected File as first arg");
        return MS_NIL_VAL();
    }
    char sep = ',';
    if (argc >= 2 && MS_IS_STRING(argv[1]) && MS_AS_STRING(argv[1])->length > 0)
        sep = MS_AS_CSTRING(argv[1])[0];

    MsObjUserdata* ud = ms_obj_userdata_new(vm, sizeof(CsvReader),
                                             csvrd_finalize, csvrd_mark,
                                             CSV_READER_TAG);
    CsvReader* rd = (CsvReader*)ud->data;
    rd->file     = MS_AS_FILE(argv[0]);
    rd->sep      = sep;
    rd->quote    = '"';
    rd->line_cap = 256;
    rd->line_buf = (char*)malloc(rd->line_cap);
    rd->line_len = 0;
    rd->line_pos = 0;
    rd->eof      = false;
    if (!rd->line_buf) {
        ms_vm_runtime_error(vm, "csv.reader: OOM");
        return MS_NIL_VAL();
    }
    return MS_OBJ_VAL(ud);
}

/* ------------------------------------------------------------------ */
/* csv.writer(file, sep=',') → CsvWriter                              */
/* ------------------------------------------------------------------ */

static MsValue csv_writer(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !MS_IS_FILE(argv[0])) {
        ms_vm_runtime_error(vm, "csv.writer: expected File as first arg");
        return MS_NIL_VAL();
    }
    char sep = ',';
    if (argc >= 2 && MS_IS_STRING(argv[1]) && MS_AS_STRING(argv[1])->length > 0)
        sep = MS_AS_CSTRING(argv[1])[0];

    MsObjUserdata* ud = ms_obj_userdata_new(vm, sizeof(CsvWriter),
                                             NULL, csvwr_mark, CSV_WRITER_TAG);
    CsvWriter* wr = (CsvWriter*)ud->data;
    wr->file = MS_AS_FILE(argv[0]);
    wr->sep  = sep;
    return MS_OBJ_VAL(ud);
}

/* ------------------------------------------------------------------ */
/* CsvReader: read one text line from file into internal buffer        */
/* ------------------------------------------------------------------ */

static bool csvrd_read_line_buf(CsvReader* rd) {
    if (rd->eof || !rd->file || !rd->file->fp) return false;
    rd->line_len = 0;
    rd->line_pos = 0;
    int c;
    while ((c = fgetc(rd->file->fp)) != EOF) {
        if ((size_t)(rd->line_len + 2) > rd->line_cap) {
            rd->line_cap *= 2;
            char* nb = (char*)realloc(rd->line_buf, rd->line_cap);
            if (!nb) return false;
            rd->line_buf = nb;
        }
        rd->line_buf[rd->line_len++] = (char)c;
        if (c == '\n') break;
    }
    if (c == EOF) {
        rd->eof = true;
        rd->file->eof = true;
        if (rd->line_len == 0) return false;
    }
    return true;
}

/* Read a complete CSV row from the file (handles quoted newlines). */
static MsObjList* csvrd_next_row(MsVM* vm, CsvReader* rd) {
    /* Accumulate raw line(s) into a combined buffer to handle quoted newlines */
    size_t raw_cap = 256;
    char* raw = (char*)malloc(raw_cap);
    if (!raw) { ms_vm_runtime_error(vm, "csv: OOM"); return NULL; }
    size_t raw_len = 0;

    /* Count open quotes to detect multi-line quoted fields */
    bool have_data = false;
    for (;;) {
        /* read one physical line */
        if (!csvrd_read_line_buf(rd)) {
            if (raw_len == 0) { free(raw); return NULL; }
            break;
        }
        have_data = true;
        /* append to raw */
        while (raw_len + rd->line_len + 2 > raw_cap) {
            raw_cap *= 2;
            char* nb = (char*)realloc(raw, raw_cap);
            if (!nb) { free(raw); ms_vm_runtime_error(vm, "csv: OOM"); return NULL; }
            raw = nb;
        }
        memcpy(raw + raw_len, rd->line_buf, rd->line_len);
        raw_len += rd->line_len;

        /* Check if we have balanced quotes (simple count) */
        int quotes = 0;
        for (size_t i = 0; i < raw_len; i++) {
            if (raw[i] == rd->quote) quotes++;
        }
        if ((quotes % 2) == 0) break; /* balanced → row complete */
        /* unbalanced: quoted field spans newline, read more */
    }
    (void)have_data;

    /* Now parse this raw block as a single row */
    size_t pos = 0;
    MsObjList* row = csv_parse_row(vm, raw, raw_len, &pos, rd->sep, rd->quote);
    free(raw);
    return row;
}

/* ------------------------------------------------------------------ */
/* CsvWriter: write_row                                                 */
/* ------------------------------------------------------------------ */

static bool csvwr_write_row(MsVM* vm, CsvWriter* wr, MsObjList* row) {
    if (!wr->file || !wr->file->fp) {
        ms_vm_runtime_error(vm, "csv.writer: file is closed");
        return false;
    }
    char sep   = wr->sep;
    char quote = '"';
    size_t buf_cap = 256;
    size_t buf_len = 0;
    char* buf = (char*)malloc(buf_cap);
    if (!buf) { ms_vm_runtime_error(vm, "csv: OOM"); return false; }

    for (int j = 0; j < row->items.count; j++) {
        if (j > 0) {
            if (buf_len + 2 >= buf_cap) {
                buf_cap *= 2;
                char* nb = (char*)realloc(buf, buf_cap);
                if (!nb) { free(buf); ms_vm_runtime_error(vm, "csv: OOM"); return false; }
                buf = nb;
            }
            buf[buf_len++] = sep;
        }
        MsValue fv = row->items.data[j];
        char* tmp_free = NULL;
        const char* field;
        int flen;
        if (MS_IS_STRING(fv)) {
            field = MS_AS_CSTRING(fv);
            flen  = MS_AS_STRING(fv)->length;
        } else {
            tmp_free = ms_value_to_cstring(fv);
            field    = tmp_free ? tmp_free : "";
            flen     = (int)strlen(field);
        }
        bool ok = csv_write_field(&buf, &buf_len, &buf_cap, field, flen, sep, quote);
        free(tmp_free);
        if (!ok) { free(buf); ms_vm_runtime_error(vm, "csv: OOM"); return false; }
    }
    /* append newline */
    if (buf_len + 2 >= buf_cap) {
        buf_cap *= 2;
        char* nb = (char*)realloc(buf, buf_cap);
        if (!nb) { free(buf); ms_vm_runtime_error(vm, "csv: OOM"); return false; }
        buf = nb;
    }
    buf[buf_len++] = '\n';
    fwrite(buf, 1, buf_len, wr->file->fp);
    free(buf);
    return true;
}

/* ================================================================== */
/* OOP method dispatch                                                  */
/* ================================================================== */

bool ms_csv_invoke(MsVM* vm, MsObjUserdata* ud,
                   MsObjString* method, int argc, MsValue* argv,
                   MsValue* out)
{
    const char* tag = ud->type_tag;
    const char* m   = method->data;

    /* ---- CsvReader ---- */
    if (strcmp(tag, CSV_READER_TAG) == 0) {
        CsvReader* rd = (CsvReader*)ud->data;

        if (strcmp(m, "read_row") == 0) {
            MsObjList* row = csvrd_next_row(vm, rd);
            *out = row ? MS_OBJ_VAL(row) : MS_NIL_VAL();
            return true;
        }
        if (strcmp(m, "read_all") == 0) {
            MsObjList* result = ms_obj_list_new(vm);
            while (true) {
                MsObjList* row = csvrd_next_row(vm, rd);
                if (!row) break;
                if (row->items.count > 0)
                    ms_value_array_push(&result->items, MS_OBJ_VAL(row));
            }
            *out = MS_OBJ_VAL(result);
            return true;
        }
        return false;
    }

    /* ---- CsvWriter ---- */
    if (strcmp(tag, CSV_WRITER_TAG) == 0) {
        CsvWriter* wr = (CsvWriter*)ud->data;

        if (strcmp(m, "write_row") == 0) {
            if (argc < 1 || !MS_IS_LIST(argv[0])) {
                ms_vm_runtime_error(vm, "write_row: expected list");
                *out = MS_NIL_VAL(); return true;
            }
            csvwr_write_row(vm, wr, MS_AS_LIST(argv[0]));
            *out = MS_NIL_VAL(); return true;
        }
        if (strcmp(m, "write_all") == 0) {
            if (argc < 1 || !MS_IS_LIST(argv[0])) {
                ms_vm_runtime_error(vm, "write_all: expected list of rows");
                *out = MS_NIL_VAL(); return true;
            }
            MsObjList* rows = MS_AS_LIST(argv[0]);
            for (int i = 0; i < rows->items.count; i++) {
                MsValue rv = rows->items.data[i];
                if (!MS_IS_LIST(rv)) {
                    ms_vm_runtime_error(vm, "write_all: each row must be a list");
                    *out = MS_NIL_VAL(); return true;
                }
                if (!csvwr_write_row(vm, wr, MS_AS_LIST(rv))) break;
            }
            *out = MS_NIL_VAL(); return true;
        }
        return false;
    }

    return false;
}

/* ================================================================== */
/* Module init                                                          */
/* ================================================================== */

void ms_module_csv_init(MsVM* vm, MsObjModule* mod) {
    static const MsNativeDef defs[] = {
        {"read_str",   csv_read_str,   -1},
        {"read_file",  csv_read_file,  -1},
        {"read_dict",  csv_read_dict,  -1},
        {"write_str",  csv_write_str,  -1},
        {"write_file", csv_write_file, -1},
        {"reader",     csv_reader,     -1},
        {"writer",     csv_writer,     -1},
        {NULL, NULL, 0}
    };
    ms_module_register_natives(vm, mod, defs);
}
