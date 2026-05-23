import "os"

// env: missing key returns nil
assert(os.env("__MSLANG_NO_SUCH_VAR__") == nil)

// setenv / env roundtrip
os.setenv("__MSLANG_TEST_KEY__", "hello")
assert(os.env("__MSLANG_TEST_KEY__") == "hello")

// unsetenv
os.setenv("__MSLANG_UNSET__", "val")
os.unsetenv("__MSLANG_UNSET__")
assert(os.env("__MSLANG_UNSET__") == nil)

// environ returns a map
var e = os.environ()
assert(type(e) == "map")

print("ok")
// expect: ok
