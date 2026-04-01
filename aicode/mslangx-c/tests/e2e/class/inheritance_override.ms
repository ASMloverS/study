class Base {
  describe() {
    print "base"
  }
}
class Derived < Base {
  describe() {
    print "derived"
  }
}
var obj = Derived()
obj.describe()