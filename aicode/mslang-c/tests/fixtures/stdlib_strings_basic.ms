import "strings"

print(strings.join(["a", "b", "c"], "-"))   // a-b-c
print(strings.repeat("ab", 3))              // ababab
print(strings.fields("  foo  bar  "))       // ["foo", "bar"]
print(strings.pad_left("42", 6))            // "    42"
print(strings.title("hello world"))         // Hello World

var s = "hello world"
print(strings.index(s, "world"))            // 6
print(strings.replace(s, "o", "0", 1))     // hell0 world
