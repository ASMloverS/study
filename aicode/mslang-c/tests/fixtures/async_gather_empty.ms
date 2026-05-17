async fun main() {
    var r = await gather([])
    print(len(r))
}
run_until_complete(main())
// expect: 0
