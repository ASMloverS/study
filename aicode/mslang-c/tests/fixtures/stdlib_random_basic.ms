import "random"

random.seed(42)

// float in [0.0, 1.0)
var f = random.float()
print(f >= 0.0)   // expect: true
print(f < 1.0)    // expect: true

// int dice [1,6]
random.seed(42)
var d = random.int(1, 6)
print(d >= 1)   // expect: true
print(d <= 6)   // expect: true

// gauss — check it produces a number (not NaN)
random.seed(42)
var g = random.gauss(0.0, 1.0)
print(g == g)   // expect: true

// shuffle preserves length and element sum
random.seed(42)
var deck = [1, 2, 3, 4, 5]
random.shuffle(deck)
print(len(deck))  // expect: 5
var s = deck[0] + deck[1] + deck[2] + deck[3] + deck[4]
print(s)  // expect: 15

// sample k=3 from 5: correct length
random.seed(42)
var samp = random.sample([10, 20, 30, 40, 50], 3)
print(len(samp))  // expect: 3

// choices with replacement k=4: correct length
random.seed(42)
var ch = random.choices(["x", "y", "z"], 4, nil)
print(len(ch))  // expect: 4
