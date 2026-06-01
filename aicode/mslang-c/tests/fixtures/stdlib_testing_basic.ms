import "testing"

// register tests
testing.run("加法", fun() {
    testing.assert_eq(1 + 1, 2)
})

testing.run("浮点近似", fun() {
    testing.assert_approx(1.0 / 3.0, 0.3333333333, 1e-9)
})

testing.run("异常", fun() {
    testing.assert_raises(fun() { throw "oops" })
})

testing.run("跳过", fun() {
    testing.skip("功能未实现")
    testing.assert(false)
})

var results = testing.run_all()
assert(results["passed"]  == 3)
assert(results["failed"]  == 0)
assert(results["skipped"] == 1)

print("stdlib_testing_basic: ok")
// expect: stdlib_testing_basic: ok
