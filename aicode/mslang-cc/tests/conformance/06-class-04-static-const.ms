class Counter {
    static var total = 0
    static func describe() -> "counters, total=${Counter.total}"
    static func add(a, b) -> a + b
    const VERSION = "1.0"
}
Counter.total += 1
