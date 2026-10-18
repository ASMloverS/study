class Base {
    func who() -> "base"
    func call() -> "Base.call"
}
trait Logger {
    func log(msg) { print("[log] ${msg}") }
}
class Impl : Base, Logger {
    func who() -> "impl"
    func call() -> "${super.call()} + Impl.call"
}
