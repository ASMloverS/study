import "hmac"

// hmac.compute one-shot
var r1 = hmac.compute("sha256", "Jefe", "what do ya want for nothing?")
assert(r1 == "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843")

// hmac.compute md5
var r2 = hmac.compute("md5", "Jefe", "what do ya want for nothing?")
assert(r2 == "750c783e6ab0b503eaa86e310a5db738")

// streaming matches one-shot
var h = hmac.new("sha256", "Jefe")
hmac.update(h, "what do ya want ")
hmac.update(h, "for nothing?")
assert(hmac.hexdigest(h) == r1)

print("stdlib_hmac_basic: ok")
// expect: stdlib_hmac_basic: ok
