async fun work(tag, ms) {
    await sleep(ms)
    return tag
}
async fun main() {
    var results = await gather([work("a", 20), work("b", 10), work("c", 5)])
    print(len(results))
    print("done")
}
run_until_complete(main())
// expect: 3
// expect: done
