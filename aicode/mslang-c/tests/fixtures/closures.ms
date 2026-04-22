fun make_counter() {
  var count = 0
  fun increment() {
    count = count + 1
    return count
  }
  return increment
}
var counter = make_counter()
print(counter())
// expect: 1
print(counter())
// expect: 2
print(counter())
// expect: 3
