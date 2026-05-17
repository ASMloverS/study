async fun boom() {
    throw "kaboom"
}
async fun main() {
    try {
        await boom()
    } catch (e) {
        print(e)
    }
    print("recovered")
}
run_until_complete(main())
// expect: kaboom
// expect: recovered
