class Animal {
  describe() {
    print "animal"
  }

  speak() {
    print "sound"
  }
}

class Dog < Animal {
  describe() {
    super.describe()
    print "dog"
  }

  speak() {
    print "woof"
  }
}

var dog = Dog()
dog.describe()
dog.speak()
