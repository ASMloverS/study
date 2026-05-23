import "os"

// name: known OS string
var n = os.name()
assert(n == "windows" or n == "linux" or n == "darwin" or n == "bsd" or n == "unknown")

// pid: positive integer
var p = os.pid()
assert(p > 0)

// argv: returns a list
var a = os.argv()
assert(type(a) == "list")

// cwd: non-empty string
var c = os.cwd()
assert(type(c) == "string")
assert(len(c) > 0)

// exec: returns an int exit code (redirect output to devnull to keep stdout clean)
var ec = os.exec("echo hello > " + os.devnull)
assert(type(ec) == "int")

print("ok")
// expect: ok
