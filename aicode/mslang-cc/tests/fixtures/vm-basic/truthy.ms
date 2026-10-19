// T011 真值规则（02 §2：仅 false 与 nil 为假）
if 0 { print("zero-t") }
if "" { print("str-t") }
if nil { print("nil-t") } else { print("nil-f") }
if false { print("f-t") } else { print("f-f") }
print(nil || "d")
print(false && 1)
print(0 && 1)
print(true ? "yes" : "no")
