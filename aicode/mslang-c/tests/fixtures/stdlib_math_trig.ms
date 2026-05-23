import "math"

// basic trig at 0
assert(math.abs(math.sin(0.0))        < 1e-10)
assert(math.abs(math.cos(0.0) - 1.0) < 1e-10)
assert(math.abs(math.tan(0.0))        < 1e-10)

// inverse trig at 0
assert(math.abs(math.asin(0.0))       < 1e-10)
assert(math.abs(math.acos(1.0))       < 1e-10)
assert(math.abs(math.atan(0.0))       < 1e-10)
assert(math.abs(math.atan2(0.0, 1.0)) < 1e-10)

// hyperbolic at 0
assert(math.abs(math.sinh(0.0))        < 1e-10)
assert(math.abs(math.cosh(0.0) - 1.0) < 1e-10)
assert(math.abs(math.tanh(0.0))        < 1e-10)

// degrees / radians
assert(math.abs(math.degrees(math.PI) - 180.0) < 1e-8)
assert(math.abs(math.radians(180.0) - math.PI) < 1e-8)

// constants
assert(math.PI  > 3.14 and math.PI  < 3.15)
assert(math.E   > 2.71 and math.E   < 2.72)
assert(math.TAU > 6.28 and math.TAU < 6.29)

print("ok")
// expect: ok
