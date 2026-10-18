class Animal {
    var name, age

    func __init__(name, age = 1) {
        this.name = name
        this.age = age
    }

    func speak() -> "..."
    func describe() -> "${this.name} (${this.age})"
}
var a = Animal("Cat", 3)
