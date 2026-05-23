import "io"
import "os"

var p = "__fix_io_file__.txt"

// write_file / read_file roundtrip
io.write_file(p, "hello world")
assert(io.read_file(p) == "hello world")

// append_file
io.append_file(p, " more")
assert(io.read_file(p) == "hello world more")

// lines
io.write_file(p, "line1\nline2\nline3")
var ls = io.lines(p)
assert(type(ls) == "list")
assert(len(ls) == 3)
assert(ls[0] == "line1")
assert(ls[1] == "line2")
assert(ls[2] == "line3")

// stdin / stdout / stderr are non-nil
assert(io.stdin  != nil)
assert(io.stdout != nil)
assert(io.stderr != nil)

// cleanup
os.remove(p)
assert(os.exists(p) == false)

print("ok")
// expect: ok
