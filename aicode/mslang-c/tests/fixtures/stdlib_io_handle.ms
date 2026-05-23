import "io"
import "os"

var p = "__fix_io_handle__.txt"

// open for write, write, close, read back
var fw = io.open(p, "w")
fw.write("via open")
fw.close()
assert(io.read_file(p) == "via open")

// open for read, readline
io.write_file(p, "first\nsecond\n")
var fr = io.open(p, "r")
var l1 = fr.readline()
assert(l1 != nil)
var l2 = fr.readline()
assert(l2 != nil)
fr.close()

// seek + tell
io.write_file(p, "abcdef")
var fs = io.open(p, "r")
var pos = fs.seek(3, 0)  // SEEK_SET
assert(pos == 3)
assert(fs.tell() == 3)
fs.close()

// cleanup
os.remove(p)
assert(os.exists(p) == false)

print("ok")
// expect: ok
