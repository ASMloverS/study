class Range3 {
    var i = 0
    func __iter__() { return this }
    func __next__() {
        this.i += 1
        return this.i < 3 ? this.i : nil
    }
}
