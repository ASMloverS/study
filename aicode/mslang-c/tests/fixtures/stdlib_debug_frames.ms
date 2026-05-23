import "debug"

// vm_stats: known keys
var st = debug.vm_stats()
assert(st["frame_used"]   >= 1)
assert(st["stack_used"]   >= 0)
assert(st["gc_bytes"]     >= 0)
assert(st["gc_threshold"] >  0)

// frame_info: current frame name
fun foo() {
    var fi = debug.frame_info()
    assert(fi["name"] == "foo")
    assert(fi["line"] >= 1)
}
foo()

// frame_info: caller at depth 1
fun inner() {
    var fi = debug.frame_info(1)
    assert(fi["name"] == "outer")
}
fun outer() { inner() }
outer()

// traceback: returns a non-empty string
var tb = debug.traceback()
assert(type(tb) == "string")
assert(len(tb) > 0)

// traceback from inside a function
fun bar() { return debug.traceback() }
var tb2 = bar()
assert(len(tb2) > 0)

// locals: returns a map
fun check_locals() {
    var x = 42
    var m = debug.locals()
    assert(type(m) == "map")
}
check_locals()

// disasm: returns a non-empty string
fun add(a, b) { return a + b }
var txt = debug.disasm(add)
assert(type(txt) == "string")
assert(len(txt) > 0)

// gc_trace: toggling does not error
debug.gc_trace(true)
debug.gc_trace(false)

print("ok")
// expect: ok
