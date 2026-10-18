async func fetchAll(urls) {
    var out = []
    for u in urls {
        var r = await fetch(u)
        out.append(r)
    }
    return out
}
var all = await fetchAll(us)
