// 两个协程 await 同一个 future；resolve 时 waiters 链表全部唤醒。
// 注：waiters 链表为头插（LIFO），实际到期顺序为 w2 先于 w1。
// 实施 ASYNC-03 后需在 test_future.c 确认顺序，再据此调整下方 expect。
async fun waiter(tag, fut) {
    await fut
    print(tag)
}
async fun main() {
    var f = sleep(10)
    var t1 = spawn waiter("w1", f)
    var t2 = spawn waiter("w2", f)
    await gather([t1, t2])
    print("end")
}
run_until_complete(main())
// expect: w2
// expect: w1
// expect: end
