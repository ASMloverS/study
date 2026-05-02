fun risky() {
  defer fun() { print("defer ran") }
  throw "fail"
}
try {
  risky()
} catch (e) {
  print("caught: " + e)
}
// expect: defer ran
// expect: caught: fail
