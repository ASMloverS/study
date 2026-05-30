/* src/stdlib/linkedlist.c - STDLIB-37: linkedlist module
 *
 * Doubly-linked list implemented as pure .ms.  All logic lives in
 * LINKEDLIST_SOURCE; this file compiles and executes that source in the module
 * context so every top-level definition becomes a module export.
 *
 * Public API:
 *   linkedlist.new()           → LinkedList   empty linked list
 *   linkedlist.from_list(lst)  → LinkedList   list pre-populated from list
 *
 * Instance methods on LinkedList:
 *   push_front(v), push_back(v), pop_front(), pop_back()
 *   peek_front(), peek_back()
 *   insert_after(node, v), insert_before(node, v), remove(node)
 *   size(), is_empty(), each(fn), to_list()
 */
#include "ms/vm.h"
#include "ms/compiler.h"

/* ------------------------------------------------------------------ */
/* Embedded .ms source                                                 */
/* ------------------------------------------------------------------ */

/* The Node class is referenced inside LinkedList methods.  If both were
   defined at module top-level, methods would resolve Node via OP_GETGLOBAL
   at call time — but after ms_vm_execute_module returns, the module globals
   table is torn down.  To avoid the dangling reference, LinkedList is defined
   inside _make_ll(NodeClass) so every method body captures NodeClass as an
   upvalue (closed-over local), not a global lookup. */
static const char LINKEDLIST_SOURCE[] =
    "class Node {\n"
    "    init(value) {\n"
    "        this.value = value\n"
    "        this.next = nil\n"
    "        this.prev = nil\n"
    "    }\n"
    "}\n"
    /* _make_ll receives Node as a parameter so LinkedList methods close over
       it as an upvalue instead of performing a global lookup at call time. */
    "var _make_ll = fun(NodeClass) {\n"
    "    class LinkedList {\n"
    "        init() {\n"
    "            this.head = nil\n"
    "            this.tail = nil\n"
    "            this._size = 0\n"
    "        }\n"
    "        push_front(v) {\n"
    "            var n = NodeClass(v)\n"
    "            if (this.head == nil) {\n"
    "                this.head = n\n"
    "                this.tail = n\n"
    "            } else {\n"
    "                n.next = this.head\n"
    "                this.head.prev = n\n"
    "                this.head = n\n"
    "            }\n"
    "            this._size = this._size + 1\n"
    "            return n\n"
    "        }\n"
    "        push_back(v) {\n"
    "            var n = NodeClass(v)\n"
    "            if (this.tail == nil) {\n"
    "                this.head = n\n"
    "                this.tail = n\n"
    "            } else {\n"
    "                n.prev = this.tail\n"
    "                this.tail.next = n\n"
    "                this.tail = n\n"
    "            }\n"
    "            this._size = this._size + 1\n"
    "            return n\n"
    "        }\n"
    "        pop_front() {\n"
    "            if (this.head == nil) {\n"
    "                error(\"linkedlist.pop_front: list is empty\")\n"
    "            }\n"
    "            var v = this.head.value\n"
    "            this.head = this.head.next\n"
    "            if (this.head == nil) {\n"
    "                this.tail = nil\n"
    "            } else {\n"
    "                this.head.prev = nil\n"
    "            }\n"
    "            this._size = this._size - 1\n"
    "            return v\n"
    "        }\n"
    "        pop_back() {\n"
    "            if (this.tail == nil) {\n"
    "                error(\"linkedlist.pop_back: list is empty\")\n"
    "            }\n"
    "            var v = this.tail.value\n"
    "            this.tail = this.tail.prev\n"
    "            if (this.tail == nil) {\n"
    "                this.head = nil\n"
    "            } else {\n"
    "                this.tail.next = nil\n"
    "            }\n"
    "            this._size = this._size - 1\n"
    "            return v\n"
    "        }\n"
    "        peek_front() {\n"
    "            if (this.head == nil) {\n"
    "                error(\"linkedlist.peek_front: list is empty\")\n"
    "            }\n"
    "            return this.head.value\n"
    "        }\n"
    "        peek_back() {\n"
    "            if (this.tail == nil) {\n"
    "                error(\"linkedlist.peek_back: list is empty\")\n"
    "            }\n"
    "            return this.tail.value\n"
    "        }\n"
    "        insert_after(node, v) {\n"
    "            var n = NodeClass(v)\n"
    "            n.prev = node\n"
    "            n.next = node.next\n"
    "            if (node.next != nil) {\n"
    "                node.next.prev = n\n"
    "            } else {\n"
    "                this.tail = n\n"
    "            }\n"
    "            node.next = n\n"
    "            this._size = this._size + 1\n"
    "            return n\n"
    "        }\n"
    "        insert_before(node, v) {\n"
    "            var n = NodeClass(v)\n"
    "            n.next = node\n"
    "            n.prev = node.prev\n"
    "            if (node.prev != nil) {\n"
    "                node.prev.next = n\n"
    "            } else {\n"
    "                this.head = n\n"
    "            }\n"
    "            node.prev = n\n"
    "            this._size = this._size + 1\n"
    "            return n\n"
    "        }\n"
    "        remove(node) {\n"
    "            if (node.prev != nil) {\n"
    "                node.prev.next = node.next\n"
    "            } else {\n"
    "                this.head = node.next\n"
    "            }\n"
    "            if (node.next != nil) {\n"
    "                node.next.prev = node.prev\n"
    "            } else {\n"
    "                this.tail = node.prev\n"
    "            }\n"
    "            this._size = this._size - 1\n"
    "            return node.value\n"
    "        }\n"
    "        size() {\n"
    "            return this._size\n"
    "        }\n"
    "        is_empty() {\n"
    "            return this._size == 0\n"
    "        }\n"
    "        each(fn) {\n"
    "            var cur = this.head\n"
    "            while (cur != nil) {\n"
    "                fn(cur.value)\n"
    "                cur = cur.next\n"
    "            }\n"
    "        }\n"
    "        to_list() {\n"
    "            var out = []\n"
    "            var cur = this.head\n"
    "            while (cur != nil) {\n"
    "                out.push(cur.value)\n"
    "                cur = cur.next\n"
    "            }\n"
    "            return out\n"
    "        }\n"
    "    }\n"
    "    return LinkedList\n"
    "}\n"
    "var _LL = _make_ll(Node)\n"
    /* Wrap constructors so _LL is captured as upvalue, not a global lookup. */
    "var _make = fun(klass) {\n"
    "    var new = fun() {\n"
    "        return klass()\n"
    "    }\n"
    "    var from_list = fun(lst) {\n"
    "        var l = klass()\n"
    "        var i = 0\n"
    "        while (i < lst.len()) {\n"
    "            l.push_back(lst[i])\n"
    "            i = i + 1\n"
    "        }\n"
    "        return l\n"
    "    }\n"
    "    return [new, from_list]\n"
    "}\n"
    "var _ctors = _make(_LL)\n"
    "var new = _ctors[0]\n"
    "var from_list = _ctors[1]\n";

/* ------------------------------------------------------------------ */
/* Module init                                                         */
/* ------------------------------------------------------------------ */

void ms_module_linkedlist_init(MsVM* vm, MsObjModule* mod) {
    MsDiagnostic diags[8];
    int diag_count = 0;
    MsObjFunction* fn = ms_compile(vm, LINKEDLIST_SOURCE, "<builtin:linkedlist>",
                                   diags, &diag_count, 8);
    if (!fn) {
        if (diag_count > 0)
            ms_vm_runtime_error(vm, "linkedlist: compile error: %s",
                                diags[0].message);
        else
            ms_vm_runtime_error(vm, "linkedlist: compile failed");
        return;
    }
    ms_vm_execute_module(vm, fn, mod);
}
