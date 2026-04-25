fun make_closure() {
  var data = "kept_alive"
  return fun() { return data }
}
var fn = make_closure()
var i = 0
while (i < 500) { var _ = "junk"; i = i + 1 }
print(fn())
