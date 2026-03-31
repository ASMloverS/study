fn outer() {
  var value = "outer"
  fn middle() {
    var value = "middle"
    return fn() {
      return value
    }
  }
  return middle()
}

var closure = outer()
print closure()