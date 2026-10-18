func f() {
    for i := 0; i < 10; i++ {
        if i == 2 { continue }
        if i == 5 { break }
        print(i)
    }
}
