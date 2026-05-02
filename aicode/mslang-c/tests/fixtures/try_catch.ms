try {
  throw "error!"
} catch (e) {
  print(e)
}
// expect: error!
