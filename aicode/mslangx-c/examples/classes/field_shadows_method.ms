class Label {
  text() {
    return "method"
  }
}

var label = Label()
label.text = "field"
print label.text
