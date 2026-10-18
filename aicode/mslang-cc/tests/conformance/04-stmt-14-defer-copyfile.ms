func copyFile(src, dst) {
    var f = io.open(src)
    defer f.close()
    return io.open(dst)
}
