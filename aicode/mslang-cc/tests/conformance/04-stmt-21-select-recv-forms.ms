select {
case v = <-ch:
    v
case <-ch:
    nil
}
