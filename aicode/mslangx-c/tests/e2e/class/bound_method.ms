class Box {
  set(value) {
    self.value = value
  }
  show() {
    print self.value
  }
}
var box = Box()
box.value = "tea"
var show = box.show
show()
var set = box.set
set("coffee")
show()