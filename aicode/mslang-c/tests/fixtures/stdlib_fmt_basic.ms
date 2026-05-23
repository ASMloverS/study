import "fmt"

print(fmt.sprintf("Hello, %s! You are %d years old.", "Alice", 30))
print(fmt.sprintf("pi = %.4f", 3.14159))
print(fmt.sprintf("hex: %x  bin: %b", 255, 10))
print(fmt.sprintf("%q", "line\n"))
print(fmt.format(42))
print(fmt.format(true))
print(fmt.format(nil))
fmt.println("count: %d", 7)
