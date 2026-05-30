import "context"
import "time"

// background: never cancelled
var bg = context.background()
print(!bg.is_cancelled())
// expect: true
print(bg.deadline() == nil)
// expect: true

// with_cancel: manual cancel
var ctx = context.with_cancel(bg)
print(!ctx.is_cancelled())
// expect: true
ctx.cancel()
print(ctx.is_cancelled())
// expect: true
ctx.cancel()  // idempotent
print(ctx.is_cancelled())
// expect: true

// parent cancel propagates to children
var p  = context.with_cancel(bg)
var c1 = context.with_cancel(p)
var c2 = context.with_cancel(c1)
p.cancel()
print(c1.is_cancelled())
// expect: true
print(c2.is_cancelled())
// expect: true

// with_value: key lookup walks chain
var vc = context.with_value(bg, "user", "alice")
var vc2 = context.with_value(vc, "role", "admin")
print(vc2.value("user"))
// expect: alice
print(vc2.value("role"))
// expect: admin
print(vc2.value("missing") == nil)
// expect: true

// done() future resolves on cancel
async fun test_done() {
    var cx = context.with_cancel(bg)
    var done = cx.done()
    cx.cancel()
    await done
    print(cx.is_cancelled())
    // expect: true
}
time.run_until_complete(test_done())

// with_timeout: auto-cancels
async fun test_timeout() {
    var cx = context.with_timeout(bg, 20)
    print(!cx.is_cancelled())
    // expect: true
    await time.sleep(50)
    print(cx.is_cancelled())
    // expect: true
}
time.run_until_complete(test_timeout())

print("context_ok")
// expect: context_ok
