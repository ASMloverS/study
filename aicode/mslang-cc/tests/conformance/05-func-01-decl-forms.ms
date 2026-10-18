func add(a, b) { return a + b }
func double(x) -> x * 2
func greet(name, prefix = "Mr.") {
    return "${prefix} ${name}"
}
func varargs(fmt, *args) {
    return [fmt, args]
}
