import "strconv"

print(strconv.format_int(255, 16))         // ff
print(strconv.parse_int("ff", 16))         // 255
print(strconv.format_float(3.14, "f", 2))  // 3.14
print(strconv.try_parse_int("bad", 10))    // nil
print(strconv.quote("he said \"hi\""))     // "he said \"hi\""
print(strconv.parse_bool("yes"))           // true
print(strconv.format_bool(false))          // false
print(strconv.try_parse_float("1.5"))      // 1.5
