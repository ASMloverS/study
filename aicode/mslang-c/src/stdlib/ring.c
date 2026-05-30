/* src/stdlib/ring.c - STDLIB-38: ring module
 *
 * Fixed-capacity circular buffer (ring buffer): FIFO semantics, overwrites
 * the oldest element when full.  Implemented as pure .ms using a list as
 * the backing store.
 *
 * Public API:
 *   ring.new(capacity)  → Ring   create ring buffer with given capacity
 *
 * Instance methods on Ring:
 *   push(v)      → any|nil   enqueue; if full, evict+return oldest, else nil
 *   pop()        → any       dequeue oldest (nil if empty)
 *   peek()       → any       view oldest without removing (nil if empty)
 *   size()       → int       current element count
 *   capacity()   → int       maximum capacity
 *   is_full()    → bool
 *   is_empty()   → bool
 *   to_list()    → list      oldest→newest order
 *   clear()      → nil
 */
#include "ms/vm.h"
#include "ms/compiler.h"

/* ------------------------------------------------------------------ */
/* Embedded .ms source                                                 */
/* ------------------------------------------------------------------ */

/* Ring uses a fixed-size list as a circular array.
 * _buf: backing list pre-filled with nils of length _cap
 * _head: index of oldest element (read pointer)
 * _len:  current number of valid elements
 *
 * Write index = (_head + _len) % _cap
 *
 * To avoid global lookups after module teardown, Ring is returned from
 * _make_ring() so every method body closes over the class via upvalue. */
static const char RING_SOURCE[] =
    "var _make_ring = fun() {\n"
    "    class Ring {\n"
    "        init(cap) {\n"
    "            if (cap < 1) {\n"
    "                error(\"ring.new: capacity must be >= 1\")\n"
    "            }\n"
    "            this._cap  = cap\n"
    "            this._head = 0\n"
    "            this._len  = 0\n"
    "            this._buf  = []\n"
    "            var i = 0\n"
    "            while (i < cap) {\n"
    "                this._buf.push(nil)\n"
    "                i = i + 1\n"
    "            }\n"
    "        }\n"
    "        push(v) {\n"
    "            var evicted = nil\n"
    "            if (this._len == this._cap) {\n"
    "                evicted = this._buf[this._head]\n"
    "                this._buf[this._head] = v\n"
    "                this._head = (this._head + 1) % this._cap\n"
    "            } else {\n"
    "                var wi = (this._head + this._len) % this._cap\n"
    "                this._buf[wi] = v\n"
    "                this._len = this._len + 1\n"
    "            }\n"
    "            return evicted\n"
    "        }\n"
    "        pop() {\n"
    "            if (this._len == 0) {\n"
    "                return nil\n"
    "            }\n"
    "            var v = this._buf[this._head]\n"
    "            this._buf[this._head] = nil\n"
    "            this._head = (this._head + 1) % this._cap\n"
    "            this._len  = this._len - 1\n"
    "            return v\n"
    "        }\n"
    "        peek() {\n"
    "            if (this._len == 0) {\n"
    "                return nil\n"
    "            }\n"
    "            return this._buf[this._head]\n"
    "        }\n"
    "        size() {\n"
    "            return this._len\n"
    "        }\n"
    "        capacity() {\n"
    "            return this._cap\n"
    "        }\n"
    "        is_full() {\n"
    "            return this._len == this._cap\n"
    "        }\n"
    "        is_empty() {\n"
    "            return this._len == 0\n"
    "        }\n"
    "        to_list() {\n"
    "            var out = []\n"
    "            var i = 0\n"
    "            while (i < this._len) {\n"
    "                out.push(this._buf[(this._head + i) % this._cap])\n"
    "                i = i + 1\n"
    "            }\n"
    "            return out\n"
    "        }\n"
    "        clear() {\n"
    "            this._head = 0\n"
    "            this._len  = 0\n"
    "            var i = 0\n"
    "            while (i < this._cap) {\n"
    "                this._buf[i] = nil\n"
    "                i = i + 1\n"
    "            }\n"
    "        }\n"
    "    }\n"
    "    return Ring\n"
    "}\n"
    "var _Ring = _make_ring()\n"
    "var _wrap = fun(klass) {\n"
    "    return fun(cap) {\n"
    "        return klass(cap)\n"
    "    }\n"
    "}\n"
    "var new = _wrap(_Ring)\n";

/* ------------------------------------------------------------------ */
/* Module init                                                         */
/* ------------------------------------------------------------------ */

void ms_module_ring_init(MsVM* vm, MsObjModule* mod) {
    MsDiagnostic diags[8];
    int diag_count = 0;
    MsObjFunction* fn = ms_compile(vm, RING_SOURCE, "<builtin:ring>",
                                   diags, &diag_count, 8);
    if (!fn) {
        if (diag_count > 0)
            ms_vm_runtime_error(vm, "ring: compile error: %s",
                                diags[0].message);
        else
            ms_vm_runtime_error(vm, "ring: compile failed");
        return;
    }
    ms_vm_execute_module(vm, fn, mod);
}
