// T019 惰性管道：genexp 真惰性（替换 T017 物化），take/drop 链式组合
var xs = range(100).toList()
var evens = (x for x in xs if x % 2 == 0)
var picked = evens.drop(2).take(5)
print(picked.toList())
var squares = (x * x for x in xs if x % 2 == 0 if x >= 4 if x <= 12)  // 迭代器单用：平方取新鲜管道
print(squares.toList())
print(sum(x * x for x in xs if x < 5))
