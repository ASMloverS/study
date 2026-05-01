fn make_counter(start) {
  var value = start
  return fn() {
    value = value + 1
    return value
  }
}

fn make_multiplier(factor) {
  return fn(value) {
    return value * factor
  }
}

fn factorial(n) {
  if (n <= 1) {
    return 1
  }
  return n * factorial(n - 1)
}

var counter = make_counter(10)
print counter()
print counter()

var triple = make_multiplier(3)
print triple(7)

print factorial(5)
