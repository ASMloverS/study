func f(a = (x for x in [1, 2, 3])) {
    return a
}
print(f())
