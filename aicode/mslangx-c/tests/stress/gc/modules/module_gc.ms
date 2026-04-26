import basic.math
import cache.counter
import cache.left
import cache.right
import pkg.tool as tool

var i = 0
var total = 0

while (i < 4) {
  var pair = [i, i + 1, i + 2, i + 3]
  total = total + pair[0] + pair[1]
  i = i + 1
}

__gc_collect__()
print math.base
print math.add_two()
print math.Label().text()
print counter.value
print left.label
print right.label
print left.counter == right.counter
print tool.name
print tool.version()
print tool.Helper().kind()
print total
