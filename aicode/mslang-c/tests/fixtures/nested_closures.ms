fun outer() {
  var x = 1
  fun middle() {
    var y = 2
    fun inner() {
      return x + y
    }
    return inner
  }
  return middle
}
print(outer()()())
// expect: 3
