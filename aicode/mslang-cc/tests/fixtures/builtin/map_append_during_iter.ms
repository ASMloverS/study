var m = {"a": 1, "b": 2, "c": 3}
var seen = []
for k in m {
    seen.append(k)
    if len(seen) == 1 {
        m["z"] = 26
    }
}
print(seen)
