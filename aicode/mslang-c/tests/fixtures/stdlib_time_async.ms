import "time"

// gather([]): empty list resolves immediately to empty list
var r = time.run_until_complete(time.gather([]))
assert(type(r) == "list")
assert(len(r) == 0)

// gather with coroutines: results collected in order
async fun val(x) { return x }
var results = time.run_until_complete(time.gather([val(1), val(2), val(3)]))
assert(type(results) == "list")
assert(len(results) == 3)
assert(results[0] == 1)
assert(results[1] == 2)
assert(results[2] == 3)

// run_until_complete with a simple async fn
async fun add(a, b) { return a + b }
var s = time.run_until_complete(add(10, 32))
assert(s == 42)

print("ok")
// expect: ok
