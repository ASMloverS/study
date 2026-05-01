class Box {
  init(value) {
    self.value = value
    return
  }

  set(value) {
    self.value = value
  }

  show() {
    print self.value
  }
}

var box = Box("tea")
box.show()

var show = box.show
show()

var set = box.set
set("coffee")
show()
