var i = 0
var total = 0

while (i < 8) {
  var pair = [i, i + 1, i + 2, i + 3]
  var triple = (i, i + 1, i + 2)

  total = total + pair[0] + triple[1] + i
  i = i + 1
}

__gc_collect__()
print total
print i
