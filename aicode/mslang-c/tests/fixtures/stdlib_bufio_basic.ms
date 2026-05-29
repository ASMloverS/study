import "io"
import "bufio"

// write_lines / read_lines
bufio.write_lines("__bufio_fixture__.txt", ["alpha", "beta", "gamma"])
var lines = bufio.read_lines("__bufio_fixture__.txt")
print(lines.len())
// expect: 3
print(lines[0])
// expect: alpha
print(lines[2])
// expect: gamma

// BufReader.read_line
io.write_file("__bufio_fix2__.txt", "first\nsecond\n")
var f = io.open("__bufio_fix2__.txt", "r")
var br = bufio.new_reader(f)
print(br.read_line())
// expect: first
print(br.read_line())
// expect: second
print(br.read_line() == nil)
// expect: true
f.close()

// BufReader.read_all
io.write_file("__bufio_fix3__.txt", "hello world")
var f2 = io.open("__bufio_fix3__.txt", "r")
var br2 = bufio.new_reader(f2)
var all = br2.read_all()
print(all.len())
// expect: 11
f2.close()

// BufWriter roundtrip
var wf = io.open("__bufio_fix4__.txt", "w")
var bw = bufio.new_writer(wf)
bw.write_line("row1")
bw.write_line("row2")
bw.flush()
wf.close()
var ls2 = bufio.read_lines("__bufio_fix4__.txt")
print(ls2.len())
// expect: 2
print(ls2[0])
// expect: row1
print(ls2[1])
// expect: row2
