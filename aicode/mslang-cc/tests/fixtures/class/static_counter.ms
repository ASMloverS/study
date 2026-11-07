class Counter {
    static var total = 0
    static func describe() -> "counters, total=${Counter.total}"
    const VERSION = "1.0"
}

class NamedCounter : Counter {
}

Counter.total += 1
Counter.total += 2
print(Counter.describe())
print(Counter.VERSION)
print(NamedCounter.describe())
