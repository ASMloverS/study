// T013 插值：嵌套段与 str/repr 形态（02 §6；10 §2）
print("a = ${1 + 2}, s = ${"in${"n"}"}")
var x = 10
print("x = ${x}, double = ${x * 2}, nested list = ${[x, "s"]}")
print("repr in interp: ${repr("q\"t")}")
