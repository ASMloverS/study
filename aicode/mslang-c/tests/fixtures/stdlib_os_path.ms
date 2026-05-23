import "os"

// basename
assert(os.basename("/foo/bar/baz.txt") == "baz.txt")
assert(os.basename("hello") == "hello")

// dirname
assert(os.dirname("/foo/bar/baz") == "/foo/bar")
assert(os.dirname("file.txt") == ".")

// splitext returns [name, ext]
var t1 = os.splitext("foo.txt")
assert(t1[0] == "foo")
assert(t1[1] == ".txt")

var t2 = os.splitext("noext")
assert(t2[0] == "noext")
assert(t2[1] == "")

// join returns a non-empty string
var j = os.join("a", "b", "c")
assert(type(j) == "string")
assert(len(j) > 0)

// constants
assert(os.sep == "/" or os.sep == "\\")
assert(type(os.linesep) == "string")
assert(os.pathsep == ":" or os.pathsep == ";")
assert(type(os.devnull) == "string")

print("ok")
// expect: ok
