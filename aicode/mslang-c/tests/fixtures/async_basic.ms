import "time"
async fun answer() {
    return 42
}
print(time.run_until_complete(answer()))
// expect: 42
