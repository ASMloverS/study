import "path"

// sep
print(path.sep == "/" or path.sep == "\\")
// expect: true

// dirname
print(path.dirname("foo/bar/baz.txt"))
// expect: foo/bar
print(path.dirname("baz.txt"))
// expect: .

// basename
print(path.basename("foo/bar/baz.txt"))
// expect: baz.txt

// ext / stem
print(path.ext("foo/bar/baz.txt"))
// expect: .txt
print(path.ext("foo/bar/baz"))
// expect:
print(path.stem("foo/bar/baz.txt"))
// expect: baz

// split
var sp = path.split("foo/bar/baz.txt")
print(sp[0])
// expect: foo/bar
print(sp[1])
// expect: baz.txt

// split_ext
var se = path.split_ext("foo/bar/baz.txt")
print(se[0])
// expect: foo/bar/baz
print(se[1])
// expect: .txt

// parts
var pts = path.parts("/usr/local/bin")
print(pts[0])
// expect: usr
print(pts[1])
// expect: local
print(pts[2])
// expect: bin

// join
var j = path.join("foo", "bar", "baz.txt")
print(j == "foo/bar/baz.txt" or j == "foo\\bar\\baz.txt")
// expect: true

// normalize
var n = path.normalize("foo//bar/../baz")
print(n == "foo/baz" or n == "foo\\baz")
// expect: true

// is_abs / is_rel
print(path.is_abs("/etc/hosts"))
// expect: true
print(path.is_abs("relative/path"))
// expect: false
print(path.is_rel("relative/path"))
// expect: true

// relative
var r = path.relative("/a/b/c", "/a")
print(r == "b/c" or r == "b\\c")
// expect: true
