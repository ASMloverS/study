class Base {
  init(name) {
    self.name = name
  }
  describe() {
    print self.name
  }
}
class Derived < Base {
  describe() {
    print "derived"
  }
  call_super() {
    super.describe()
  }
}
var obj = Derived("base-name")
obj.describe()
obj.call_super()