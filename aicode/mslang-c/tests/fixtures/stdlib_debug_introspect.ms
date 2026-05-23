import "debug"

// typeof: all basic types
assert(debug.typeof(nil)        == "nil")
assert(debug.typeof(true)       == "bool")
assert(debug.typeof(42)         == "int")
assert(debug.typeof(3.14)       == "number")
assert(debug.typeof("hi")       == "string")
assert(debug.typeof([1, 2])     == "list")
assert(debug.typeof({"a": 1})   == "map")

fun f() {}
assert(debug.typeof(f) == "function")

// is_native: closures are not native
assert(debug.is_native(f)  == false)
assert(debug.is_native(42) == false)

// is_closure: closures are closures
assert(debug.is_closure(f)   == true)
assert(debug.is_closure(nil) == false)

// upvalues: no upvalues for a plain function
var uv = debug.upvalues(f)
assert(type(uv) == "list")
assert(len(uv) == 0)

// upvalues: captured variable shows up
fun make_adder(x) {
    fun adder(y) { return x + y }
    return adder
}
var add5 = make_adder(5)
var uv2 = debug.upvalues(add5)
assert(len(uv2) >= 1)

print("ok")
// expect: ok
