async fun main() {
    await sleep(0)
    print("after-await")
    return "ok"
}
print(run_until_complete(main()))
// expect: after-await
// expect: ok
