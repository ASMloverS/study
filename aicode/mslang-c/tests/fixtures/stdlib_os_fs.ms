import "os"

// mkdir / isdir / rmdir
var d = "__fix_os_fs__"
os.mkdir(d)
assert(os.isdir(d))
assert(os.exists(d))
os.rmdir(d)
assert(os.exists(d) == false)

// makedirs / recursive
var nested = "__fix_os_makedirs__/__inner__"
os.makedirs(nested)
assert(os.isdir(nested))
os.rmdir(nested)
os.rmdir("__fix_os_makedirs__")
assert(os.exists("__fix_os_makedirs__") == false)

// rename (dirs)
os.mkdir("__fix_os_ren_src__")
os.rename("__fix_os_ren_src__", "__fix_os_ren_dst__")
assert(os.exists("__fix_os_ren_src__") == false)
assert(os.isdir("__fix_os_ren_dst__"))
os.rmdir("__fix_os_ren_dst__")

// listdir: empty dir
var ld = "__fix_os_listdir__"
os.mkdir(ld)
var items = os.listdir(ld)
assert(type(items) == "list")
assert(len(items) == 0)
os.rmdir(ld)

// stat: directory fields
var sd = "__fix_os_stat__"
os.mkdir(sd)
var st = os.stat(sd)
assert(st["isdir"]  == true)
assert(st["isfile"] == false)
os.rmdir(sd)

// realpath
var rd = "__fix_os_realpath__"
os.mkdir(rd)
var rp = os.realpath(rd)
assert(type(rp) == "string")
assert(len(rp) > 0)
os.rmdir(rd)

// isfile / exists for non-existent
assert(os.exists("__no_such_path_xyz__")  == false)
assert(os.isfile("__no_such_path_xyz__")  == false)
assert(os.isdir("__no_such_path_xyz__")   == false)

print("ok")
// expect: ok
