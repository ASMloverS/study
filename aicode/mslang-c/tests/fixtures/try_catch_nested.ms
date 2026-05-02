try {
  try {
    throw "inner"
  } catch (e) {
    print("caught: " + e)
    throw "rethrown"
  }
} catch (e2) {
  print("outer: " + e2)
}
// expect: caught: inner
// expect: outer: rethrown
