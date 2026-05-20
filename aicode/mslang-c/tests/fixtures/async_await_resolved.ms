import "time"
async fun main() {
    await time.sleep(0)
    print("after-await")
    return "ok"
}
print(time.run_until_complete(main()))
// expect: after-await
// expect: ok
