async fun answer() {
    return 42
}
print(run_until_complete(answer()))
// expect: 42
