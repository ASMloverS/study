import "io"

var tmp = "docs/features/stdlib/manual/examples/_tmp_io_test.txt"

// 写文件
io.write_file(tmp, "Hello, mslang!\nLine 2\nLine 3")

// 读整文件
var content = io.read_file(tmp)
print(content)

// 按行读
var lines = io.lines(tmp)
print(len(lines))      // 3
print(lines[0])        // Hello, mslang!
print(lines[2])        // Line 3

// 追加写
io.append_file(tmp, "\nLine 4")
var lines2 = io.lines(tmp)
print(len(lines2))     // 4

// 流式文件句柄
var f = io.open(tmp, "r")
var first = f.readline()
print(first)           // Hello, mslang!\n  （readline 保留尾部换行）
print(f.eof())         // false
f.close()

// 二进制写+读
import "buffer"
var btmp = "docs/features/stdlib/manual/examples/_tmp_io_bytes.bin"
var buf = buffer.from_hex("cafebabe")
io.write_bytes(btmp, buf)
var rbuf = io.read_bytes(btmp)
print(rbuf.to_hex())   // cafebabe
print(rbuf.len())      // 4

// 标准流（仅写，不读 stdin 以免阻塞）
io.stdout.write("written via io.stdout\n")

// 清理临时文件
import "os"
os.remove(tmp)
os.remove(btmp)
print(os.exists(tmp))   // false
