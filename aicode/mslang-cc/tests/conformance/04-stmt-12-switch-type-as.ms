switch type(v) {
case int, bigint:
    print("number")
case string as s:
    print(len(s))
case Point as p:
    p.move(1, 1)
default:
    print(type(v))
}
