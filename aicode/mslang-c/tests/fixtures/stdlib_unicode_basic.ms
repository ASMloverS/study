import "unicode"

// is_letter
print(unicode.is_letter("A"))
// expect: true
print(unicode.is_letter("z"))
// expect: true
print(unicode.is_letter("1"))
// expect: false

// is_digit
print(unicode.is_digit("5"))
// expect: true
print(unicode.is_digit("a"))
// expect: false

// is_space
print(unicode.is_space(" "))
// expect: true
print(unicode.is_space("x"))
// expect: false

// is_upper / is_lower
print(unicode.is_upper("A"))
// expect: true
print(unicode.is_lower("a"))
// expect: true
print(unicode.is_upper("a"))
// expect: false

// is_punct
print(unicode.is_punct("."))
// expect: true
print(unicode.is_punct("a"))
// expect: false

// is_graphic
print(unicode.is_graphic("!"))
// expect: true
print(unicode.is_graphic(0))
// expect: false

// to_upper / to_lower / to_title
print(unicode.to_upper("a"))
// expect: A
print(unicode.to_lower("Z"))
// expect: z
print(unicode.to_title("b"))
// expect: B

// ord / chr
print(unicode.ord("A"))
// expect: 65
print(unicode.chr(97))
// expect: a

// rune_count
print(unicode.rune_count("hello"))
// expect: 5
print(unicode.rune_count(""))
// expect: 0

// rune_at
print(unicode.rune_at("hello", 0))
// expect: h
print(unicode.rune_at("hello", 4))
// expect: o

// runes
var r = unicode.runes("abc")
print(r.len())
// expect: 3
print(r[0])
// expect: a
print(r[2])
// expect: c

// encode_utf8
var b = unicode.encode_utf8(65)
print(b.len())
// expect: 1
print(b.get(0))
// expect: 65

// decode_utf8
var d = unicode.decode_utf8("hello")
print(d[0])
// expect: h
print(d[1])
// expect: 1

// valid_utf8
print(unicode.valid_utf8("hello"))
// expect: true
print(unicode.valid_utf8(""))
// expect: true
