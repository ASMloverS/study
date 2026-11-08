class Greeter {
    var name

    func __init__(name) {
        this.name = name
    }

    func greet() -> "hi ${this.name}"
    func viaBound() -> this.greet()
}

var g = Greeter("ms")
var m = g.greet
print(m())
print(g.viaBound())
print(Greeter.greet(g))
