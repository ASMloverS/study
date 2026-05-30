import "sync"
import "time"

// Once
var calls = 0
var o = sync.new_once(fun() { calls = calls + 1 })
o.do()
o.do()
print(calls)
// expect: 1
print(o.done())
// expect: true

// WaitGroup basic (already zero)
async fun test_wg() {
    var wg = sync.new_wait_group()
    wg.add(1)
    wg.done()
    await wg.wait()
    print("wg_done")
    // expect: wg_done
}
time.run_until_complete(test_wg())

// Buffered channel
async fun test_ch() {
    var ch = sync.new_channel(2)
    await ch.send("a")
    await ch.send("b")
    print(await ch.recv())
    // expect: a
    print(await ch.recv())
    // expect: b
    print(ch.len())
    // expect: 0
    ch.close()
    print(ch.is_closed())
    // expect: true
}
time.run_until_complete(test_ch())

// race
async fun test_race() {
    var fast = time.sleep(5)
    var slow = time.sleep(100)
    await sync.race([fast, slow])
    print("race_ok")
    // expect: race_ok
}
time.run_until_complete(test_race())

// gather delegation
async fun test_gather() {
    async fun num(x) { return x }
    var r = await sync.gather([num(10), num(20)])
    print(r.len())
    // expect: 2
}
time.run_until_complete(test_gather())
