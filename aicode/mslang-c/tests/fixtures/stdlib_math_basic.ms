import "math"

// abs
assert(math.abs(-3.5) == 3.5)
assert(math.abs(2.0)  == 2.0)

// floor / ceil / trunc
assert(math.floor(1.9)   == 1.0)
assert(math.floor(-1.1)  == -2.0)
assert(math.ceil(1.1)    == 2.0)
assert(math.trunc(-1.9)  == -1.0)

// round (ties-to-even)
assert(math.round(1.4) == 1.0)
assert(math.round(1.6) == 2.0)
assert(math.round(0.5) == 0.0)
assert(math.round(1.5) == 2.0)

// sign
assert(math.sign(5.0)  ==  1)
assert(math.sign(-3.0) == -1)
assert(math.sign(0.0)  ==  0)

// fmod
assert(math.fmod(5.0, 3.0) == 2.0)

// min / max
assert(math.min(3.0, 1.0, 2.0) == 1.0)
assert(math.max(3.0, 1.0, 2.0) == 3.0)

// clamp
assert(math.clamp(10.0, 0.0, 5.0) == 5.0)
assert(math.clamp(-1.0, 0.0, 5.0) == 0.0)
assert(math.clamp(3.0,  0.0, 5.0) == 3.0)

// sum
assert(math.sum([1.0, 2.0, 3.0]) == 6.0)
assert(math.sum([]) == 0.0)

print("ok")
// expect: ok
