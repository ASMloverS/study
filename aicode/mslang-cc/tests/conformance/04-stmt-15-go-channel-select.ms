go worker(1, 2)
ch <- v
v2 = <-ch
<-ch
select {
case r = <-ch1:
    print(r)
case ch2 <- x:
    print("sent")
default:
    print("idle")
}
