var unit = (1,)
var empty = ()
var t = (1, "a", 1)
print(unit)
print(empty)
print(t.count(1))
print(t.indexOf("a"))
print(t.contains("a"))
print(t.toList())
var m = {}
m[(1, 2)] = "pair"
print(m[(1, 2)])
print((1, "a") == (1, "a"))
print((1,) == (2,))
var x = 0
var y = 0
x, y = (10, 20)
print(x + y)
for k, v in {1: "one"}.items() {
  print("${k}:${v}")
}
