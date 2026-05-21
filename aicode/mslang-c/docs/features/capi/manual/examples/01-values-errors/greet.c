#include "ms/module.h"
#include <stdio.h>
#include <string.h>

static const MsModuleApi* g_api;

/* greet(name) -> "Hello, <name>!"
   Demonstrates: is_string, val_to_cstring, make_string, raise */
static MsValue fn_greet(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1 || !g_api->is_string(argv[0]))
        return g_api->raise(vm, "greet(name): expected string");
    /* val_to_cstring pointer is valid until the next alloc-triggering api call */
    const char* name = g_api->val_to_cstring(argv[0]);
    char buf[256];
    int len = snprintf(buf, sizeof(buf), "Hello, %s!", name);
    /* make_string copies buf — safe even though name pointed into GC memory */
    return g_api->make_string(vm, buf, len);
}

/* type_of(val) -> "nil" | "bool" | "int" | "number" | "string" | "list" | "map" | "other"
   Demonstrates: is_nil, is_bool, is_int, is_number, is_string, is_list, is_map */
static MsValue fn_type_of(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 1)
        return g_api->raise(vm, "type_of(val): expected 1 argument");
    MsValue v = argv[0];
    const char* t;
    if      (g_api->is_nil(v))    t = "nil";
    else if (g_api->is_bool(v))   t = "bool";
    else if (g_api->is_int(v))    t = "int";
    else if (g_api->is_number(v)) t = "number";
    else if (g_api->is_string(v)) t = "string";
    else if (g_api->is_list(v))   t = "list";
    else if (g_api->is_map(v))    t = "map";
    else                          t = "other";
    return g_api->make_string(vm, t, (int)strlen(t));
}

/* add(a, b) -> int (if both int) or number
   Demonstrates: is_int, is_number, val_to_int, val_to_number, make_int, make_number */
static MsValue fn_add(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2)
        return g_api->raise(vm, "add(a, b): expected 2 arguments");
    MsValue a = argv[0], b = argv[1];
    int a_ok = g_api->is_int(a) || g_api->is_number(a);
    int b_ok = g_api->is_int(b) || g_api->is_number(b);
    if (!a_ok || !b_ok)
        return g_api->raise(vm, "add(a, b): expected numbers");
    if (g_api->is_int(a) && g_api->is_int(b))
        return g_api->make_int(g_api->val_to_int(a) + g_api->val_to_int(b));
    double da = g_api->is_int(a) ? (double)g_api->val_to_int(a) : g_api->val_to_number(a);
    double db = g_api->is_int(b) ? (double)g_api->val_to_int(b) : g_api->val_to_number(b);
    return g_api->make_number(da + db);
}

/* summary(name, score) -> map {name, score, passed, tags}
   Demonstrates: make_map, make_string, make_bool, make_list, map_set, list_push
   Also demonstrates val_to_cstring lifetime: copy before any alloc call */
static MsValue fn_summary(MsVM* vm, int argc, MsValue* argv) {
    if (argc < 2)
        return g_api->raise(vm, "summary(name, score): expected 2 arguments");
    if (!g_api->is_string(argv[0]))
        return g_api->raise(vm, "summary: name must be a string");
    if (!g_api->is_int(argv[1]) && !g_api->is_number(argv[1]))
        return g_api->raise(vm, "summary: score must be a number");

    /* Copy name into a stack buffer BEFORE any alloc-triggering api call.
       make_map below would otherwise invalidate the val_to_cstring pointer. */
    const char* name_ptr = g_api->val_to_cstring(argv[0]);
    char name_buf[256];
    snprintf(name_buf, sizeof(name_buf), "%s", name_ptr);

    double score = g_api->is_int(argv[1])
        ? (double)g_api->val_to_int(argv[1])
        : g_api->val_to_number(argv[1]);

    MsValue map = g_api->make_map(vm);  /* name_ptr invalid after this point */

    g_api->map_set(vm, map,
        g_api->make_string(vm, "name", 4),
        g_api->make_string(vm, name_buf, (int)strlen(name_buf)));

    g_api->map_set(vm, map,
        g_api->make_string(vm, "score", 5),
        argv[1]);

    g_api->map_set(vm, map,
        g_api->make_string(vm, "passed", 6),
        g_api->make_bool(score >= 60.0));

    MsValue tags = g_api->make_list(vm);
    g_api->list_push(vm, tags, g_api->make_string(vm, "student", 7));
    g_api->map_set(vm, map, g_api->make_string(vm, "tags", 4), tags);

    return map;
}

MS_EXPORT void ms_module_init(const MsModuleApi* api, MsVM* vm, MsObjModule* mod) {
    if (api->version < 1) {
        api->raise(vm, "greet: requires MsModuleApi v1");
        return;
    }
    g_api = api;
    api->def_native(vm, mod, "greet",    fn_greet,    1);
    api->def_native(vm, mod, "type_of",  fn_type_of,  1);
    api->def_native(vm, mod, "add",      fn_add,      2);
    api->def_native(vm, mod, "summary",  fn_summary,  2);
}
