class Counter {
  value() {
    return self.n
  }
}

var counter = Counter()
counter.n = 0
for (var i = 0; i < 5000; i = i + 1) {
  counter.n = counter.n + 1
}

print counter.value()
