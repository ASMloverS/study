import "time"
async fun main() {
    await time.sleep(5)
    print("a")
    await time.sleep(5)
    print("b")
    await time.sleep(5)
    print("c")
}
time.run_until_complete(main())
// expect: a
// expect: b
// expect: c
