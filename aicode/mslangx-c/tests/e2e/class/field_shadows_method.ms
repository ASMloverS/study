class Bag {
  label() {
    return "method"
  }
}
var first = Bag()
var second = Bag()
print first.label
first.label = "field"
print first.label
print second.label