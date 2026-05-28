import "regexp"

// is_match basic
print(regexp.is_match("\\d+", "abc123"))
// expect: true

print(regexp.is_match("\\d+", "abc"))
// expect: false

// search
var m = regexp.search("\\d+", "abc42xyz")
print(m.matched)
// expect: 42

print(m.start)
// expect: 3

// match (anchored)
var m2 = regexp.match("\\d+", "123abc")
print(m2.matched)
// expect: 123

var m3 = regexp.match("\\d+", "abc123")
print(m3 == nil)
// expect: true

// capture groups
var mg = regexp.search("(\\w+)@(\\w+)", "user@host")
print(mg.matched)
// expect: user@host

print(mg.group(1))
// expect: user

print(mg.group(2))
// expect: host

// case-insensitive
print(regexp.is_match("(?i)hello", "HELLO"))
// expect: true

// find_all
var all = regexp.find_all("\\d+", "a1b22c333")
print(all.len())
// expect: 3

print(all[0].matched)
// expect: 1

print(all[1].matched)
// expect: 22

print(all[2].matched)
// expect: 333

// replace (string)
print(regexp.replace("\\d+", "a1b2c3", "X"))
// expect: aXbXcX

// replace_n
print(regexp.replace_n("\\d+", "a1b2c3", "X", 2))
// expect: aXbXc3

// split
var parts = regexp.split("\\s+", "hello world")
print(parts.len())
// expect: 2

print(parts[0])
// expect: hello

print(parts[1])
// expect: world

// compile + Regex methods
var re = regexp.compile("\\d+")
var rm = re.search("abc99def")
print(rm.matched)
// expect: 99

var rall = re.find_all("1 22 333")
print(rall.len())
// expect: 3

// alternation
print(regexp.is_match("cat|dog", "I have a dog"))
// expect: true

print(regexp.is_match("cat|dog", "I have a fish"))
// expect: false

// non-capturing group
var nc = regexp.search("(?:ab)+(c)", "ababc")
print(nc.group(1))
// expect: c
