func f(a, b = 1, *r) { return [a, b, r] }
f(1)
f(1, 2)
f(1, 2, 3, 4)
