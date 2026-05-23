import "math"

// sqrt
assert(math.sqrt(9) == 3.0)
assert(math.sqrt(0) == 0.0)

// pow
assert(math.pow(2.0, 10.0) == 1024.0)

// exp / log roundtrip
assert(math.abs(math.log(math.exp(1.0)) - 1.0) < 1e-10)

// log2 / log10
assert(math.log2(8.0)    == 3.0)
assert(math.log10(100.0) == 2.0)

// hypot
assert(math.hypot(3.0, 4.0) == 5.0)

print("ok")
// expect: ok
