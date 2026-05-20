#include "test_assert.h"
#include "ms/vm.h"
#include "ms/object.h"
#include "ms/value.h"
#include "ms/stdlib/objfile.h"
#include "ms/stdlib/objbuffer.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* ── ObjFile tests ──────────────────────────────────────────────── */

static void test_objfile_new(void) {
    MsVM vm;
    ms_vm_init(&vm);

    FILE* fp = tmpfile();
    TEST_ASSERT(fp != NULL);

    MsObjFile* f = ms_obj_file_new(&vm, fp, MS_FILE_TEXT);
    TEST_ASSERT(f != NULL);
    TEST_ASSERT(f->obj.type == MS_OBJ_FILE);
    TEST_ASSERT(f->fp == fp);
    TEST_ASSERT(f->mode == MS_FILE_TEXT);
    TEST_ASSERT(!f->eof);

    /* macros */
    MsValue v = MS_OBJ_VAL(f);
    TEST_ASSERT(MS_IS_FILE(v));
    TEST_ASSERT(MS_AS_FILE(v) == f);

    ms_vm_free(&vm);
}

static void test_objfile_methods_write_read(void) {
    MsVM vm;
    ms_vm_init(&vm);

    FILE* fp = tmpfile();
    MsObjFile* f = ms_obj_file_new(&vm, fp, MS_FILE_TEXT);

    MsObjString* m_write = ms_obj_string_copy(&vm, "write", 5);
    MsObjString* m_flush = ms_obj_string_copy(&vm, "flush", 5);
    MsObjString* m_seek  = ms_obj_string_copy(&vm, "seek",  4);
    MsObjString* m_read  = ms_obj_string_copy(&vm, "read",  4);
    MsObjString* m_tell  = ms_obj_string_copy(&vm, "tell",  4);
    MsObjString* m_close = ms_obj_string_copy(&vm, "close", 5);
    MsObjString* m_eof   = ms_obj_string_copy(&vm, "eof",   3);
    MsValue out;

    /* write "hello" */
    MsValue wdata = MS_OBJ_VAL(ms_obj_string_copy(&vm, "hello", 5));
    TEST_ASSERT(ms_objfile_invoke(&vm, f, m_write, 1, &wdata, &out));

    /* flush */
    TEST_ASSERT(ms_objfile_invoke(&vm, f, m_flush, 0, NULL, &out));

    /* seek to start: seek(0, 0) */
    MsValue seek_args[2] = { MS_INT_VAL(0), MS_INT_VAL(0) };
    TEST_ASSERT(ms_objfile_invoke(&vm, f, m_seek, 2, seek_args, &out));

    /* tell → 0 */
    TEST_ASSERT(ms_objfile_invoke(&vm, f, m_tell, 0, NULL, &out));
    TEST_ASSERT(MS_IS_INT(out));
    TEST_ASSERT_EQ(MS_AS_INT(out), (ms_i64)0);

    /* read(-1) → ObjString "hello" */
    MsValue read_arg = MS_INT_VAL(-1);
    TEST_ASSERT(ms_objfile_invoke(&vm, f, m_read, 1, &read_arg, &out));
    TEST_ASSERT(MS_IS_STRING(out));
    TEST_ASSERT_STR_EQ(MS_AS_CSTRING(out), "hello");

    /* eof() should be true now */
    TEST_ASSERT(ms_objfile_invoke(&vm, f, m_eof, 0, NULL, &out));
    TEST_ASSERT(MS_IS_BOOL(out));
    TEST_ASSERT(MS_AS_BOOL(out));

    /* close */
    TEST_ASSERT(ms_objfile_invoke(&vm, f, m_close, 0, NULL, &out));
    TEST_ASSERT(f->fp == NULL);

    ms_vm_free(&vm);
}

/* ── ObjBuffer tests ────────────────────────────────────────────── */

static void test_objbuffer_new(void) {
    MsVM vm;
    ms_vm_init(&vm);

    MsObjBuffer* b = ms_obj_buffer_new(&vm, 16);
    TEST_ASSERT(b != NULL);
    TEST_ASSERT(b->obj.type == MS_OBJ_BUFFER);
    TEST_ASSERT(b->len == 0);
    TEST_ASSERT(b->cap >= 16);

    MsValue v = MS_OBJ_VAL(b);
    TEST_ASSERT(MS_IS_BUFFER(v));
    TEST_ASSERT(MS_AS_BUFFER(v) == b);

    ms_vm_free(&vm);
}

static void test_objbuffer_from_bytes(void) {
    MsVM vm;
    ms_vm_init(&vm);

    const uint8_t bytes[] = { 'h', 'e', 'l', 'l', 'o' };
    MsObjBuffer* b = ms_obj_buffer_from_bytes(&vm, bytes, 5);
    TEST_ASSERT(b != NULL);
    TEST_ASSERT(b->len == 5);
    TEST_ASSERT(memcmp(b->data, bytes, 5) == 0);

    ms_vm_free(&vm);
}

static void test_objbuffer_methods(void) {
    MsVM vm;
    ms_vm_init(&vm);

    const uint8_t hello[] = { 'h', 'e', 'l', 'l', 'o' };
    MsObjBuffer* b = ms_obj_buffer_from_bytes(&vm, hello, 5);

    MsValue bv = MS_OBJ_VAL(b);
    MsObjString* m_len    = ms_obj_string_copy(&vm, "len",    3);
    MsObjString* m_get    = ms_obj_string_copy(&vm, "get",    3);
    MsObjString* m_set    = ms_obj_string_copy(&vm, "set",    3);
    MsObjString* m_to_str = ms_obj_string_copy(&vm, "to_str", 6);
    MsObjString* m_to_hex = ms_obj_string_copy(&vm, "to_hex", 6);
    MsObjString* m_append = ms_obj_string_copy(&vm, "append", 6);
    MsObjString* m_copy   = ms_obj_string_copy(&vm, "copy",   4);
    MsObjString* m_equals = ms_obj_string_copy(&vm, "equals", 6);
    MsValue out;
    (void)bv;

    /* len() == 5 */
    TEST_ASSERT(ms_objbuffer_invoke(&vm, b, m_len, 0, NULL, &out));
    TEST_ASSERT(MS_IS_INT(out));
    TEST_ASSERT_EQ(MS_AS_INT(out), (ms_i64)5);

    /* get(0) == 'h' = 104 */
    MsValue idx0 = MS_INT_VAL(0);
    TEST_ASSERT(ms_objbuffer_invoke(&vm, b, m_get, 1, &idx0, &out));
    TEST_ASSERT(MS_IS_INT(out));
    TEST_ASSERT_EQ(MS_AS_INT(out), (ms_i64)'h');

    /* set(0, 'H') */
    MsValue set_args[2] = { MS_INT_VAL(0), MS_INT_VAL('H') };
    TEST_ASSERT(ms_objbuffer_invoke(&vm, b, m_set, 2, set_args, &out));
    TEST_ASSERT(b->data[0] == 'H');

    /* to_str() == "Hello" */
    TEST_ASSERT(ms_objbuffer_invoke(&vm, b, m_to_str, 0, NULL, &out));
    TEST_ASSERT(MS_IS_STRING(out));
    TEST_ASSERT_STR_EQ(MS_AS_CSTRING(out), "Hello");

    /* to_hex() == "48656c6c6f" */
    TEST_ASSERT(ms_objbuffer_invoke(&vm, b, m_to_hex, 0, NULL, &out));
    TEST_ASSERT(MS_IS_STRING(out));
    TEST_ASSERT_STR_EQ(MS_AS_CSTRING(out), "48656c6c6f");

    /* append(" world") via another buffer */
    const uint8_t world[] = { ' ', 'w', 'o', 'r', 'l', 'd' };
    MsObjBuffer* bw = ms_obj_buffer_from_bytes(&vm, world, 6);
    MsValue bwv = MS_OBJ_VAL(bw);
    TEST_ASSERT(ms_objbuffer_invoke(&vm, b, m_append, 1, &bwv, &out));
    TEST_ASSERT(b->len == 11);

    /* copy() */
    TEST_ASSERT(ms_objbuffer_invoke(&vm, b, m_copy, 0, NULL, &out));
    TEST_ASSERT(MS_IS_BUFFER(out));
    MsObjBuffer* bc = MS_AS_BUFFER(out);
    TEST_ASSERT(bc->len == b->len);
    TEST_ASSERT(memcmp(bc->data, b->data, b->len) == 0);

    /* equals(copy) == true */
    MsValue bcv = out;
    TEST_ASSERT(ms_objbuffer_invoke(&vm, b, m_equals, 1, &bcv, &out));
    TEST_ASSERT(MS_IS_BOOL(out));
    TEST_ASSERT(MS_AS_BOOL(out));

    ms_vm_free(&vm);
}

/* ── ms_builtin_invoke dispatch ─────────────────────────────────── */

static void test_builtin_invoke_dispatch(void) {
    MsVM vm;
    ms_vm_init(&vm);

    /* ObjFile */
    FILE* fp = tmpfile();
    MsObjFile* f = ms_obj_file_new(&vm, fp, MS_FILE_TEXT);
    MsValue fv = MS_OBJ_VAL(f);
    MsObjString* m_close = ms_obj_string_copy(&vm, "close", 5);
    MsValue out;
    TEST_ASSERT(ms_builtin_invoke(&vm, fv, m_close, 0, NULL, &out));

    /* ObjBuffer */
    MsObjBuffer* b = ms_obj_buffer_new(&vm, 4);
    MsValue bv = MS_OBJ_VAL(b);
    MsObjString* m_len = ms_obj_string_copy(&vm, "len", 3);
    TEST_ASSERT(ms_builtin_invoke(&vm, bv, m_len, 0, NULL, &out));
    TEST_ASSERT(MS_IS_INT(out));
    TEST_ASSERT_EQ(MS_AS_INT(out), (ms_i64)0);

    ms_vm_free(&vm);
}

/* ── main ─────────────────────────────────────────────────────────── */

int main(void) {
    test_objfile_new();
    test_objfile_methods_write_read();
    test_objbuffer_new();
    test_objbuffer_from_bytes();
    test_objbuffer_methods();
    test_builtin_invoke_dispatch();
    printf("test_capi_handle_types: all passed\n");
    return 0;
}
