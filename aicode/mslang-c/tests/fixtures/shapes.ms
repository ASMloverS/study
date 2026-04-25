class Point {
  init(x, y) {
    this.x = x
    this.y = y
  }
}
// Same property order -> same shape -> IC hit
var p1 = Point(1, 2)
var p2 = Point(3, 4)
print(p1.x + p2.x)
// expect: 4
print(p1.y + p2.y)
// expect: 6
