import "time"
async fun inner(x) {
    await time.sleep(0)
    return x * 2
}
async fun outer() {
    var a = await inner(3)
    var b = await inner(4)
    return a + b
}
print(time.run_until_complete(outer()))
// expect: 14
