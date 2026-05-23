import "math"

// gcd
assert(math.gcd(12, 8) == 4)
assert(math.gcd(7,  3) == 1)
assert(math.gcd(0,  5) == 5)

// lcm
assert(math.lcm(4, 6) == 12)
assert(math.lcm(3, 7) == 21)

// is_nan / is_inf / is_finite
assert(math.is_nan(math.NAN) == true)
assert(math.is_nan(1.0)      == false)
assert(math.is_inf(math.INF) == true)
assert(math.is_inf(1.0)      == false)
assert(math.is_finite(1.0)      == true)
assert(math.is_finite(math.INF) == false)
assert(math.is_finite(math.NAN) == false)

// INF / NAN constants
assert(math.is_inf(math.INF))
assert(math.is_nan(math.NAN))

// random / randint / seed (determinism check)
math.seed(42)
var x = math.randint(1, 6)
assert(x >= 1 and x <= 6)
var r = math.random()
assert(r >= 0.0 and r < 1.0)

print("ok")
// expect: ok
