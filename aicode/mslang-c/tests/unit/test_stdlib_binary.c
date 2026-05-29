/* tests/unit/test_stdlib_binary.c - STDLIB-32: binary module */
#include "ms/vm.h"
#include <stdio.h>
#include <stdlib.h>

static MsInterpretResult run_snippet(const char* src) {
    MsVM* vm = (MsVM*)malloc(sizeof(MsVM));
    if (!vm) { fprintf(stderr, "OOM\n"); exit(1); }
    ms_vm_init(vm);
    MsInterpretResult r = ms_vm_interpret(vm, src, "<test>");
    ms_vm_free(vm);
    free(vm);
    return r;
}

static void check_ok(const char* src, int line) {
    MsInterpretResult r = run_snippet(src);
    if (r != MS_INTERPRET_OK) {
        fprintf(stderr, "FAIL test_stdlib_binary.c:%d: expected OK\n", line);
        exit(1);
    }
}

static void check_err(const char* src, int line) {
    MsInterpretResult r = run_snippet(src);
    if (r != MS_INTERPRET_RUNTIME_ERROR) {
        fprintf(stderr, "FAIL test_stdlib_binary.c:%d: expected RUNTIME_ERROR\n", line);
        exit(1);
    }
}

#define RUN_OK(src)  check_ok((src),  __LINE__)
#define RUN_ERR(src) check_err((src), __LINE__)

/* ---- calc_size ---- */
static void test_calc_size(void) {
    RUN_OK(
        "import \"binary\"\n"
        "assert(binary.calc_size(\">B\") == 1)\n"
        "assert(binary.calc_size(\">H\") == 2)\n"
        "assert(binary.calc_size(\">I\") == 4)\n"
        "assert(binary.calc_size(\">Q\") == 8)\n"
        "assert(binary.calc_size(\">IH\") == 6)\n"
        "assert(binary.calc_size(\"<2H\") == 4)\n"
        "assert(binary.calc_size(\">4s\") == 4)\n"
        "assert(binary.calc_size(\">d\") == 8)\n"
        "assert(binary.calc_size(\">f\") == 4)\n"
    );
}

/* ---- pack / unpack big-endian ---- */
static void test_pack_unpack_be(void) {
    RUN_OK(
        "import \"binary\"\n"
        "var b = binary.pack(\">IH\", 0xDEADBEEF, 0x1234)\n"
        "assert(b.len() == 6)\n"
        "assert(b.get(0) == 0xDE)\n"
        "assert(b.get(1) == 0xAD)\n"
        "assert(b.get(2) == 0xBE)\n"
        "assert(b.get(3) == 0xEF)\n"
        "assert(b.get(4) == 0x12)\n"
        "assert(b.get(5) == 0x34)\n"
    );
    RUN_OK(
        "import \"binary\"\n"
        "var b = binary.pack(\">IH\", 0xDEADBEEF, 0x1234)\n"
        "var vals = binary.unpack(\">IH\", b)\n"
        "assert(vals[0] == 3735928559)\n"  /* 0xDEADBEEF */
        "assert(vals[1] == 0x1234)\n"
    );
}

/* ---- pack / unpack little-endian ---- */
static void test_pack_unpack_le(void) {
    RUN_OK(
        "import \"binary\"\n"
        "var b = binary.pack(\"<I\", 0x01020304)\n"
        "assert(b.get(0) == 0x04)\n"
        "assert(b.get(1) == 0x03)\n"
        "assert(b.get(2) == 0x02)\n"
        "assert(b.get(3) == 0x01)\n"
    );
    RUN_OK(
        "import \"binary\"\n"
        "var b = binary.pack(\"<I\", 0x01020304)\n"
        "var vals = binary.unpack(\"<I\", b)\n"
        "assert(vals[0] == 0x01020304)\n"
    );
}

/* ---- signed integers ---- */
static void test_signed_integers(void) {
    RUN_OK(
        "import \"binary\"\n"
        "var b = binary.pack(\">b\", -1)\n"
        "assert(b.get(0) == 255)\n"
        "var v = binary.unpack(\">b\", b)\n"
        "assert(v[0] == -1)\n"
    );
    RUN_OK(
        "import \"binary\"\n"
        "var b = binary.pack(\">h\", -256)\n"
        "assert(b.len() == 2)\n"
        "var v = binary.unpack(\">h\", b)\n"
        "assert(v[0] == -256)\n"
    );
    RUN_OK(
        "import \"binary\"\n"
        "var b = binary.pack(\">i\", -1)\n"
        "assert(b.len() == 4)\n"
        "var v = binary.unpack(\">i\", b)\n"
        "assert(v[0] == -1)\n"
    );
}

/* ---- float / double ---- */
static void test_float_double(void) {
    RUN_OK(
        "import \"binary\"\n"
        /* pack/unpack double round-trip */
        "var b = binary.pack(\">d\", 3.14)\n"
        "assert(b.len() == 8)\n"
        "var v = binary.unpack(\">d\", b)\n"
        "var diff = v[0] - 3.14\n"
        "if (diff < 0) { diff = 0 - diff }\n"
        "assert(diff < 0.0001)\n"
    );
}

/* ---- repeat count ---- */
static void test_repeat_count(void) {
    RUN_OK(
        "import \"binary\"\n"
        "var b = binary.pack(\">3B\", 1, 2, 3)\n"
        "assert(b.len() == 3)\n"
        "assert(b.get(0) == 1)\n"
        "assert(b.get(1) == 2)\n"
        "assert(b.get(2) == 3)\n"
        "var vals = binary.unpack(\">3B\", b)\n"
        "assert(vals[0] == 1)\n"
        "assert(vals[1] == 2)\n"
        "assert(vals[2] == 3)\n"
    );
}

/* ---- unpack with offset ---- */
static void test_unpack_offset(void) {
    RUN_OK(
        "import \"binary\"\n"
        "var b = binary.pack(\">BH\", 0xFF, 0x1234)\n"
        "var vals = binary.unpack(\">H\", b, 1)\n"
        "assert(vals[0] == 0x1234)\n"
    );
}

/* ---- pack_into ---- */
static void test_pack_into(void) {
    RUN_OK(
        "import \"binary\"\n"
        "import \"buffer\"\n"
        "var b = buffer.new(4)\n"
        "binary.pack_into(b, 0, \">HH\", 0xAABB, 0xCCDD)\n"
        "assert(b.get(0) == 0xAA)\n"
        "assert(b.get(1) == 0xBB)\n"
        "assert(b.get(2) == 0xCC)\n"
        "assert(b.get(3) == 0xDD)\n"
    );
}

/* ---- varint encode / decode ---- */
static void test_varint(void) {
    /* 300 = 0xAC 0x02 in unsigned LEB128 */
    RUN_OK(
        "import \"binary\"\n"
        "var b = binary.varint_encode(300)\n"
        "assert(b.len() == 2)\n"
        "assert(b.get(0) == 0xAC)\n"
        "assert(b.get(1) == 0x02)\n"
        "var r = binary.varint_decode(b)\n"
        "assert(r[0] == 300)\n"
        "assert(r[1] == 2)\n"
    );
    /* small value: 1 → 0x01 */
    RUN_OK(
        "import \"binary\"\n"
        "var b = binary.varint_encode(1)\n"
        "assert(b.len() == 1)\n"
        "assert(b.get(0) == 1)\n"
        "var r = binary.varint_decode(b)\n"
        "assert(r[0] == 1)\n"
        "assert(r[1] == 1)\n"
    );
    /* zero */
    RUN_OK(
        "import \"binary\"\n"
        "var b = binary.varint_encode(0)\n"
        "assert(b.len() == 1)\n"
        "assert(b.get(0) == 0)\n"
        "var r = binary.varint_decode(b)\n"
        "assert(r[0] == 0)\n"
    );
    /* decode with offset: pack two bytes then decode at offset 1 */
    RUN_OK(
        "import \"binary\"\n"
        "var combined = binary.pack(\">BB\", 0x99, 0x7F)\n"
        "var r = binary.varint_decode(combined, 1)\n"
        "assert(r[0] == 127)\n"
        "assert(r[1] == 1)\n"
    );
}

/* ---- svarint encode / decode ---- */
static void test_svarint(void) {
    /* zigzag: 0→0, -1→1, 1→2, -2→3, 2→4 */
    RUN_OK(
        "import \"binary\"\n"
        "var b = binary.svarint_encode(0)\n"
        "var r = binary.svarint_decode(b)\n"
        "assert(r[0] == 0)\n"
    );
    RUN_OK(
        "import \"binary\"\n"
        "var b = binary.svarint_encode(-1)\n"
        "var r = binary.svarint_decode(b)\n"
        "assert(r[0] == -1)\n"
    );
    RUN_OK(
        "import \"binary\"\n"
        "var b = binary.svarint_encode(1)\n"
        "var r = binary.svarint_decode(b)\n"
        "assert(r[0] == 1)\n"
    );
    RUN_OK(
        "import \"binary\"\n"
        "var b = binary.svarint_encode(-300)\n"
        "var r = binary.svarint_decode(b)\n"
        "assert(r[0] == -300)\n"
    );
}

/* ---- convenience read_uN ---- */
static void test_read_un(void) {
    RUN_OK(
        "import \"binary\"\n"
        "var b = binary.pack(\">BHI\", 0xAB, 0x1234, 0xDEADBEEF)\n"
        "assert(binary.read_u8(b, 0) == 0xAB)\n"
        "assert(binary.read_u16_be(b, 1) == 0x1234)\n"
        "assert(binary.read_u32_be(b, 3) == 0xDEADBEEF)\n"
    );
    RUN_OK(
        "import \"binary\"\n"
        "var b = binary.pack(\"<HI\", 0x1234, 0xDEADBEEF)\n"
        "assert(binary.read_u16_le(b, 0) == 0x1234)\n"
        "assert(binary.read_u32_le(b, 2) == 0xDEADBEEF)\n"
    );
    RUN_OK(
        "import \"binary\"\n"
        "var b = binary.pack(\">Q\", 0x0102030405060708)\n"
        "assert(binary.read_u64_be(b, 0) == 0x0102030405060708)\n"
    );
    RUN_OK(
        "import \"binary\"\n"
        "var b = binary.pack(\"<Q\", 0x0102030405060708)\n"
        "assert(binary.read_u64_le(b, 0) == 0x0102030405060708)\n"
    );
}

/* ---- error cases ---- */
static void test_errors(void) {
    /* wrong format arg type */
    RUN_ERR(
        "import \"binary\"\n"
        "binary.calc_size(42)\n"
    );
    /* not enough arguments for pack */
    RUN_ERR(
        "import \"binary\"\n"
        "binary.pack(\">HH\", 1)\n"
    );
    /* unpack buffer too short */
    RUN_ERR(
        "import \"binary\"\n"
        "var b = binary.pack(\">B\", 1)\n"
        "binary.unpack(\">H\", b)\n"
    );
    /* read_u8 out of range */
    RUN_ERR(
        "import \"binary\"\n"
        "var b = binary.pack(\">B\", 0x42)\n"
        "binary.read_u8(b, 5)\n"
    );
}

int main(void) {
    test_calc_size();
    test_pack_unpack_be();
    test_pack_unpack_le();
    test_signed_integers();
    test_float_double();
    test_repeat_count();
    test_unpack_offset();
    test_pack_into();
    test_varint();
    test_svarint();
    test_read_un();
    test_errors();
    printf("test_stdlib_binary: all tests passed\n");
    return 0;
}
