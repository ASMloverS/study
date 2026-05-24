import "json"

// nil/bool
print(json.encode(nil))
print(json.encode(true))
print(json.encode(false))

// int and num
print(json.encode(42))
print(json.encode(-7))

// string
print(json.encode("hello"))

// list
print(json.encode([1, 2, 3]))

// map roundtrip
var obj = {"name": "Alice", "age": 30}
var s = json.encode(obj)
var v = json.decode(s)
print(v["name"])
print(v["age"])

// is_valid
print(json.is_valid("null"))
print(json.is_valid("bad"))

// try_decode
print(json.try_decode("42"))
print(json.try_decode("bad json"))

// int vs num distinction
var iv = json.decode("1")
var fv = json.decode("1.0")
print(iv == 1)
print(fv == 1.0)
