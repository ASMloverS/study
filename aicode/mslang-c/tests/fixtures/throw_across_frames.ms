fun dangerous() {
  throw "boom"
}
try {
  dangerous()
} catch (e) {
  print(e)
}
// expect: boom
