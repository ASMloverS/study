/* tests/unit/test_stdlib_template.c - STDLIB-46: template module */
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
        fprintf(stderr, "FAIL test_stdlib_template.c:%d: expected OK\n", line);
        exit(1);
    }
}

static void check_fail(const char* src, int line) {
    MsInterpretResult r = run_snippet(src);
    if (r == MS_INTERPRET_OK) {
        fprintf(stderr, "FAIL test_stdlib_template.c:%d: expected RUNTIME_ERROR\n", line);
        exit(1);
    }
}

#define RUN_OK(src)   check_ok((src),   __LINE__)
#define RUN_FAIL(src) check_fail((src), __LINE__)

/* ------------------------------------------------------------------ */

static void test_basic_var(void) {
    RUN_OK(
        "import \"template\"\n"
        "var r = template.render(\"Hello, {{name}}!\", {\"name\": \"World\"})\n"
        "assert(r == \"Hello, World!\")\n"
    );
}

static void test_missing_key(void) {
    /* Missing key should produce empty string, not error */
    RUN_OK(
        "import \"template\"\n"
        "var r = template.render(\"{{missing}}\", {})\n"
        "assert(r == \"\")\n"
    );
}

static void test_escape(void) {
    /* In .ms source: "\\{{amount}}" → scanner produces string \{{amount}}
       Template engine sees \{{ and outputs literal {{ */
    RUN_OK(
        "import \"template\"\n"
        "var r = template.render(\"price: \\\\{{amount}}\", {})\n"
        "assert(r == \"price: {{amount}}\")\n"
    );
}

static void test_comment(void) {
    RUN_OK(
        "import \"template\"\n"
        "var r = template.render(\"a{{! this is a comment }}b\", {})\n"
        "assert(r == \"ab\")\n"
    );
}

static void test_if_true(void) {
    RUN_OK(
        "import \"template\"\n"
        "var r = template.render(\"{{if show}}yes{{end}}\", {\"show\": true})\n"
        "assert(r == \"yes\")\n"
    );
}

static void test_if_false(void) {
    RUN_OK(
        "import \"template\"\n"
        "var r = template.render(\"{{if show}}yes{{end}}\", {\"show\": false})\n"
        "assert(r == \"\")\n"
    );
}

static void test_if_else(void) {
    RUN_OK(
        "import \"template\"\n"
        "var r = template.render(\"{{if admin}}A{{else}}U{{end}}\","
        " {\"admin\": true})\n"
        "assert(r == \"A\")\n"
    );
    RUN_OK(
        "import \"template\"\n"
        "var r = template.render(\"{{if admin}}A{{else}}U{{end}}\","
        " {\"admin\": false})\n"
        "assert(r == \"U\")\n"
    );
}

static void test_for_list(void) {
    RUN_OK(
        "import \"template\"\n"
        "var r = template.render(\"{{for x in items}}{{x}},{{end}}\","
        " {\"items\": [1, 2, 3]})\n"
        "assert(r == \"1,2,3,\")\n"
    );
}

static void test_for_empty_list(void) {
    RUN_OK(
        "import \"template\"\n"
        "var r = template.render(\"{{for x in items}}{{x}}{{end}}\","
        " {\"items\": []})\n"
        "assert(r == \"\")\n"
    );
}

static void test_nested_if_in_for(void) {
    RUN_OK(
        "import \"template\"\n"
        "var r = template.render("
        "  \"{{for n in nums}}{{if n}}Y{{else}}N{{end}}{{end}}\","
        "  {\"nums\": [1, 0, 2]})\n"
        "assert(r == \"YNY\")\n"
    );
}

static void test_parse_then_render(void) {
    RUN_OK(
        "import \"template\"\n"
        "var t = template.parse(\"Hi {{who}}\")\n"
        "var r = template.render_tmpl(t, {\"who\": \"Bob\"})\n"
        "assert(r == \"Hi Bob\")\n"
    );
}

static void test_parse_reuse(void) {
    /* Same compiled template rendered twice with different contexts */
    RUN_OK(
        "import \"template\"\n"
        "var t = template.parse(\"{{x}}\")\n"
        "var r1 = template.render_tmpl(t, {\"x\": \"foo\"})\n"
        "var r2 = template.render_tmpl(t, {\"x\": \"bar\"})\n"
        "assert(r1 == \"foo\")\n"
        "assert(r2 == \"bar\")\n"
    );
}

static void test_expr_tag(void) {
    /* {{= name }} is treated the same as {{name}} */
    RUN_OK(
        "import \"template\"\n"
        "var r = template.render(\"{{= val }}\", {\"val\": \"42\"})\n"
        "assert(r == \"42\")\n"
    );
}

static void test_number_rendering(void) {
    RUN_OK(
        "import \"template\"\n"
        "var r = template.render(\"{{n}}\", {\"n\": 99})\n"
        "assert(r == \"99\")\n"
    );
}

static void test_bool_rendering(void) {
    RUN_OK(
        "import \"template\"\n"
        "var r = template.render(\"{{v}}\", {\"v\": true})\n"
        "assert(r == \"true\")\n"
    );
}

static void test_nil_renders_empty(void) {
    RUN_OK(
        "import \"template\"\n"
        "var r = template.render(\"[{{v}}]\", {\"v\": nil})\n"
        "assert(r == \"[]\")\n"
    );
}

static void test_bad_parse_raises(void) {
    /* Unclosed {{ should raise a runtime error */
    RUN_FAIL(
        "import \"template\"\n"
        "template.render(\"{{ unclosed\", {})\n"
    );
}

/* ------------------------------------------------------------------ */

int main(void) {
    test_basic_var();
    test_missing_key();
    test_escape();
    test_comment();
    test_if_true();
    test_if_false();
    test_if_else();
    test_for_list();
    test_for_empty_list();
    test_nested_if_in_for();
    test_parse_then_render();
    test_parse_reuse();
    test_expr_tag();
    test_number_rendering();
    test_bool_rendering();
    test_nil_renders_empty();
    test_bad_parse_raises();
    printf("test_stdlib_template: all passed\n");
    return 0;
}
