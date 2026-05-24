import "maps"

var m = {"a": 1, "b": 2, "c": 3}

// keys / values / count
print(maps.count(m))

// clone
var c = maps.clone(m)
print(c["a"])

// merge
var m2 = maps.merge(m, {"c": 99, "d": 4})
print(m2["c"])
print(m2["d"])

// get / has / pop
print(maps.get(m, "a"))
print(maps.get(m, "z", 0))
print(maps.has(m, "b"))
print(maps.has(m, "x"))

// filter
var f = maps.filter(m, fun(k,v){ return v > 1 })
print(maps.count(f))

// map_values
var mv = maps.map_values(m, fun(k,v){ return v * 10 })
print(mv["a"])

// invert
var inv = maps.invert(m)
print(inv[1])

// any / all
print(maps.any(m, fun(k,v){ return v > 2 }))
print(maps.all(m, fun(k,v){ return v > 0 }))

// from_pairs
var fp = maps.from_pairs([["x",10],["y",20]])
print(fp["x"])

// from_keys
var fk = maps.from_keys(["p","q"], 0)
print(fk["p"])

// group_by
var words = ["apple", "banana", "avocado", "blueberry"]
var grouped = maps.group_by(words, fun(w){ return w[0] })
print(len(grouped["a"]))
print(len(grouped["b"]))

// zip
var z = maps.zip(["a","b","c"], [1,2,3])
print(z["a"])
