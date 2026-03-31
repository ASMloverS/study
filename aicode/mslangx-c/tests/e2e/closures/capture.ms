fn make_counter(start) {
  var value = start
  return fn() {
    value = value + 1
    return value
  }
}

var counter = make_counter(10)
print counter()
print counter()