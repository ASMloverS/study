async fun main() {
    await sleep(5)
    print("a")
    await sleep(5)
    print("b")
    await sleep(5)
    print("c")
}
run_until_complete(main())
// expect: a
// expect: b
// expect: c
