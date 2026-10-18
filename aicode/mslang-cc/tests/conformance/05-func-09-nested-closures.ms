func adder(a) {
    return func(b) {
        return func(c) -> a + b + c
    }
}
var t = adder(1)(2)(3)
