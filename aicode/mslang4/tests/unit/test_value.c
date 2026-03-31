#include "value.h"
#include <math.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>

static void test_value_constructors(void) {
	MsValue nv = ms_nil_val();
	if (nv.type != MS_VAL_NIL) {
		fprintf(stderr, "FAIL: nil type mismatch\n");
		exit(1);
	}
	if (!ms_is_nil(nv)) {
		fprintf(stderr, "FAIL: ms_is_nil returned false for nil\n");
		exit(1);
	}

	MsValue bv = ms_bool_val(true);
	if (bv.type != MS_VAL_BOOL) {
		fprintf(stderr, "FAIL: bool type mismatch\n");
		exit(1);
	}
	if (!ms_is_bool(bv)) {
		fprintf(stderr, "FAIL: ms_is_bool returned false for bool\n");
		exit(1);
	}
	if (ms_as_bool(bv) != true) {
		fprintf(stderr, "FAIL: bool value not true\n");
		exit(1);
	}

	MsValue bf = ms_bool_val(false);
	if (ms_as_bool(bf) != false) {
		fprintf(stderr, "FAIL: bool value not false\n");
		exit(1);
	}

	MsValue num = ms_number_val(3.14);
	if (num.type != MS_VAL_NUMBER) {
		fprintf(stderr, "FAIL: number type mismatch\n");
		exit(1);
	}
	if (!ms_is_number(num)) {
		fprintf(stderr, "FAIL: ms_is_number returned false for number\n");
		exit(1);
	}
	if (fabs(ms_as_number(num) - 3.14) > 1e-10) {
		fprintf(stderr, "FAIL: number value not 3.14\n");
		exit(1);
	}

	if (ms_is_nil(ms_bool_val(true))) {
		fprintf(stderr, "FAIL: is_nil(true) should be false\n");
		exit(1);
	}
	if (ms_is_bool(ms_number_val(1))) {
		fprintf(stderr, "FAIL: is_bool(1) should be false\n");
		exit(1);
	}
	if (ms_is_number(ms_nil_val())) {
		fprintf(stderr, "FAIL: is_number(nil) should be false\n");
		exit(1);
	}
	if (ms_is_obj(ms_number_val(1))) {
		fprintf(stderr, "FAIL: is_obj(1) should be false\n");
		exit(1);
	}

	if (sizeof(MsValue) != 16) {
		fprintf(stderr, "FAIL: sizeof(MsValue) = %zu, expected 16\n",
			sizeof(MsValue));
		exit(1);
	}

	printf("  test_value_constructors PASSED\n");
}

static void test_value_equality(void) {
	if (!ms_values_equal(ms_number_val(42), ms_number_val(42))) {
		fprintf(stderr, "FAIL: 42 == 42\n");
		exit(1);
	}
	if (ms_values_equal(ms_number_val(1), ms_number_val(2))) {
		fprintf(stderr, "FAIL: 1 != 2\n");
		exit(1);
	}
	if (!ms_values_equal(ms_bool_val(true), ms_bool_val(true))) {
		fprintf(stderr, "FAIL: true == true\n");
		exit(1);
	}
	if (ms_values_equal(ms_bool_val(true), ms_bool_val(false))) {
		fprintf(stderr, "FAIL: true != false\n");
		exit(1);
	}
	if (!ms_values_equal(ms_nil_val(), ms_nil_val())) {
		fprintf(stderr, "FAIL: nil == nil\n");
		exit(1);
	}
	if (ms_values_equal(ms_nil_val(), ms_bool_val(false))) {
		fprintf(stderr, "FAIL: nil != false\n");
		exit(1);
	}
	printf("  test_value_equality PASSED\n");
}

static void test_falsey(void) {
	if (!ms_is_falsey(ms_nil_val())) {
		fprintf(stderr, "FAIL: nil should be falsey\n");
		exit(1);
	}
	if (!ms_is_falsey(ms_bool_val(false))) {
		fprintf(stderr, "FAIL: false should be falsey\n");
		exit(1);
	}
	if (ms_is_falsey(ms_bool_val(true))) {
		fprintf(stderr, "FAIL: true should not be falsey\n");
		exit(1);
	}
	if (ms_is_falsey(ms_number_val(0))) {
		fprintf(stderr, "FAIL: 0 should not be falsey\n");
		exit(1);
	}
	if (ms_is_falsey(ms_number_val(1))) {
		fprintf(stderr, "FAIL: 1 should not be falsey\n");
		exit(1);
	}
	printf("  test_falsey PASSED\n");
}

static void test_print_value(void) {
	const char *tmp_path = "test_print_value_tmp.txt";
	FILE *tmp = freopen(tmp_path, "w", stdout);
	if (!tmp) {
		fprintf(stderr, "FAIL: freopen returned NULL\n");
		exit(1);
	}

	ms_print_value(ms_nil_val());
	ms_print_value(ms_bool_val(true));
	ms_print_value(ms_bool_val(false));
	ms_print_value(ms_number_val(42));
	fflush(stdout);
	fclose(stdout);

	FILE *check = fopen(tmp_path, "r");
	if (!check) {
		fprintf(stderr, "FAIL: could not reopen temp file\n");
		exit(1);
	}
	char buf[256];
	size_t n = fread(buf, 1, sizeof(buf) - 1, check);
	buf[n] = '\0';
	fclose(check);
	remove(tmp_path);

	freopen("CON", "a", stdout);

	if (memcmp(buf, "niltruefalse42", 14) != 0) {
		fprintf(stderr, "FAIL: print output '%s', expected 'niltruefalse42'\n", buf);
		exit(1);
	}
	printf("  test_print_value PASSED\n");
}

static void test_value_array_basic(void) {
	MsValueArray arr;
	ms_value_array_init(&arr);
	if (arr.values != NULL || arr.count != 0 || arr.capacity != 0) {
		fprintf(stderr, "FAIL: init state wrong\n");
		exit(1);
	}

	ms_value_array_write(&arr, ms_number_val(1));
	ms_value_array_write(&arr, ms_number_val(2));
	ms_value_array_write(&arr, ms_number_val(3));
	if (arr.count != 3) {
		fprintf(stderr, "FAIL: count expected 3, got %d\n", arr.count);
		exit(1);
	}
	if (ms_as_number(arr.values[0]) != 1.0) {
		fprintf(stderr, "FAIL: values[0] != 1.0\n");
		exit(1);
	}
	if (ms_as_number(arr.values[1]) != 2.0) {
		fprintf(stderr, "FAIL: values[1] != 2.0\n");
		exit(1);
	}
	if (ms_as_number(arr.values[2]) != 3.0) {
		fprintf(stderr, "FAIL: values[2] != 3.0\n");
		exit(1);
	}

	ms_value_array_free(&arr);
	if (arr.values != NULL || arr.count != 0 || arr.capacity != 0) {
		fprintf(stderr, "FAIL: free state wrong\n");
		exit(1);
	}
	printf("  test_value_array_basic PASSED\n");
}

static void test_value_array_growth(void) {
	MsValueArray arr;
	ms_value_array_init(&arr);

	for (int i = 0; i < 200; i++) {
		ms_value_array_write(&arr, ms_number_val(i));
	}
	if (arr.count != 200) {
		fprintf(stderr, "FAIL: growth count expected 200, got %d\n",
			arr.count);
		exit(1);
	}
	for (int i = 0; i < 200; i++) {
		if (ms_as_number(arr.values[i]) != (double)i) {
			fprintf(stderr, "FAIL: growth values[%d] expected %d\n",
				i, i);
			exit(1);
		}
	}

	ms_value_array_free(&arr);
	printf("  test_value_array_growth PASSED\n");
}

int main(void) {
	printf("Running value tests...\n");
	test_value_constructors();
	test_value_equality();
	test_falsey();
	test_print_value();
	test_value_array_basic();
	test_value_array_growth();
	printf("All value tests passed.\n");
	return 0;
}
