func counter() {
    var n = 0
    return func() { n += 1; return n }
}
var c = counter()
