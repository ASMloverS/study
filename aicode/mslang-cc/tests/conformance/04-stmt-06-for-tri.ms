for ; ; { break }
func f(n) {
    for i := 0; i < n; i++ { print(i) }
    for i := 0; ; { break }
    for ; n > 0; { n -= 1 }
}
