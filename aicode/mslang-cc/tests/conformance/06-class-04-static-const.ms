class Counter {
    static var total = 0
    static func describe() -> "counters, total=${Counter.total}"
    const VERSION = "1.0"
}
Counter.total += 1
