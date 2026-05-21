import "djb2"

h = djb2.create()

djb2.update(h, "hello")
djb2.update(h, " ")
djb2.update(h, "world")
print(djb2.digest(h))

djb2.reset(h)
djb2.update(h, "hello world")
print(djb2.digest(h))

h2 = djb2.create()
djb2.update(h2, "abc")
print(djb2.digest(h2))
