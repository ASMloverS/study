/* src/stdlib/deque.c - STDLIB-36: deque module
 *
 * Double-ended queue implemented as pure .ms (circular-array simulation
 * using a list).  All logic lives in DEQUE_SOURCE; this file compiles and
 * executes that source in the module context so every top-level definition
 * becomes a module export.
 *
 * Public API:
 *   deque.new()           → Deque   empty deque
 *   deque.from_list(lst)  → Deque   deque pre-populated from list
 *
 * Instance methods on Deque:
 *   push_back(v), push_front(v), pop_back(), pop_front()
 *   peek_back(), peek_front(), get(i), size(), is_empty()
 *   clear(), to_list()
 */
#include "ms/vm.h"
#include "ms/compiler.h"

/* ------------------------------------------------------------------ */
/* Embedded .ms source                                                 */
/* ------------------------------------------------------------------ */

static const char DEQUE_SOURCE[] =
    "class Deque {\n"
    "    init() {\n"
    "        this._data = []\n"
    "    }\n"
    "    push_back(v) {\n"
    "        this._data.push(v)\n"
    "    }\n"
    "    push_front(v) {\n"
    "        var tmp = [v]\n"
    "        var i = 0\n"
    "        while (i < this._data.len()) {\n"
    "            tmp.push(this._data[i])\n"
    "            i = i + 1\n"
    "        }\n"
    "        this._data = tmp\n"
    "    }\n"
    "    pop_back() {\n"
    "        if (this._data.len() == 0) {\n"
    "            error(\"deque.pop_back: deque is empty\")\n"
    "        }\n"
    "        return this._data.pop()\n"
    "    }\n"
    "    pop_front() {\n"
    "        if (this._data.len() == 0) {\n"
    "            error(\"deque.pop_front: deque is empty\")\n"
    "        }\n"
    "        var v = this._data[0]\n"
    "        var tmp = []\n"
    "        var i = 1\n"
    "        while (i < this._data.len()) {\n"
    "            tmp.push(this._data[i])\n"
    "            i = i + 1\n"
    "        }\n"
    "        this._data = tmp\n"
    "        return v\n"
    "    }\n"
    "    peek_back() {\n"
    "        if (this._data.len() == 0) {\n"
    "            error(\"deque.peek_back: deque is empty\")\n"
    "        }\n"
    "        return this._data[this._data.len() - 1]\n"
    "    }\n"
    "    peek_front() {\n"
    "        if (this._data.len() == 0) {\n"
    "            error(\"deque.peek_front: deque is empty\")\n"
    "        }\n"
    "        return this._data[0]\n"
    "    }\n"
    "    get(i) {\n"
    "        return this._data[i]\n"
    "    }\n"
    "    size() {\n"
    "        return this._data.len()\n"
    "    }\n"
    "    is_empty() {\n"
    "        return this._data.len() == 0\n"
    "    }\n"
    "    clear() {\n"
    "        this._data = []\n"
    "    }\n"
    "    to_list() {\n"
    "        var out = []\n"
    "        var i = 0\n"
    "        while (i < this._data.len()) {\n"
    "            out.push(this._data[i])\n"
    "            i = i + 1\n"
    "        }\n"
    "        return out\n"
    "    }\n"
    "}\n"
    /* Wrap constructors in a local scope so Deque is captured as an upvalue,
       not looked up as a global at call time (globals are gone post-execute). */
    "var _make = fun(klass) {\n"
    "    var new = fun() {\n"
    "        return klass()\n"
    "    }\n"
    "    var from_list = fun(lst) {\n"
    "        var d = klass()\n"
    "        var i = 0\n"
    "        while (i < lst.len()) {\n"
    "            d.push_back(lst[i])\n"
    "            i = i + 1\n"
    "        }\n"
    "        return d\n"
    "    }\n"
    "    return [new, from_list]\n"
    "}\n"
    "var _ctors = _make(Deque)\n"
    "var new = _ctors[0]\n"
    "var from_list = _ctors[1]\n";

/* ------------------------------------------------------------------ */
/* Module init                                                         */
/* ------------------------------------------------------------------ */

void ms_module_deque_init(MsVM* vm, MsObjModule* mod) {
    MsDiagnostic diags[8];
    int diag_count = 0;
    MsObjFunction* fn = ms_compile(vm, DEQUE_SOURCE, "<builtin:deque>",
                                   diags, &diag_count, 8);
    if (!fn) {
        if (diag_count > 0)
            ms_vm_runtime_error(vm, "deque: compile error: %s",
                                diags[0].message);
        else
            ms_vm_runtime_error(vm, "deque: compile failed");
        return;
    }
    ms_vm_execute_module(vm, fn, mod);
}
