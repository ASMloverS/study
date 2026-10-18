class Point {
    func __init__(x, y) { this.x = x; this.y = y }
    func __eq__(o) { return this.x == o.x && this.y == o.y }
    func __hash__() { return this.x * 31 + this.y }
}
