fun make_garbage() {
  var i = 0
  while (i < 1000) {
    var s = "temp" + "garbage"
    i = i + 1
  }
  return "done"
}
print(make_garbage())
