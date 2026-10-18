class P {
    var x = 0
    func get() { return this.x }
}
var p = P()
var m = p.get
var v = m()
var w = P.get(p)
