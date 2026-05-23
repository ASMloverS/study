import "os"

// 进程信息
print(os.name())    // windows 或 linux
print(os.pid())     // 当前进程 PID

// 环境变量
var home = os.env("PATH")
print(home != nil)   // true

// 当前工作目录
var cwd = os.cwd()
print(os.isdir(cwd))   // true

// 路径操作
var p = os.join("docs", "features", "stdlib")
print(p)                                    // docs\features\stdlib 或 docs/features/stdlib
print(os.basename("docs/features/stdlib"))  // stdlib
print(os.dirname("docs/features/stdlib"))   // docs/features
print(os.splitext("file.tar.gz"))           // ["file.tar", ".gz"]

// 文件系统检测
print(os.exists("docs"))          // true
print(os.isdir("docs"))           // true
print(os.isfile("CMakeLists.txt")) // true

// 目录列表
var entries = os.listdir("docs/features/stdlib")
print(len(entries) > 0)   // true

// stat：获取文件元信息
var s = os.stat("CMakeLists.txt")
print(s["isfile"])   // true
print(s["isdir"])    // false
print(s["size"] > 0) // true

// mkdir + rmdir（临时目录，用完清理）
os.mkdir("docs/features/stdlib/manual/examples/_tmp_os_test")
print(os.isdir("docs/features/stdlib/manual/examples/_tmp_os_test"))  // true
os.rmdir("docs/features/stdlib/manual/examples/_tmp_os_test")
print(os.exists("docs/features/stdlib/manual/examples/_tmp_os_test")) // false
