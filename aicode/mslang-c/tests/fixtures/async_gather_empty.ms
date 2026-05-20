import "time"
async fun main() {
    var r = await time.gather([])
    print(len(r))
}
time.run_until_complete(main())
// expect: 0
