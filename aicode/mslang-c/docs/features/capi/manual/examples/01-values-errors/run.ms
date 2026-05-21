import greet

print(greet.greet("World"))        # Hello, World!

print(greet.type_of(42))           # int
print(greet.type_of(3.14))         # number
print(greet.type_of("hi"))         # string
print(greet.type_of(nil))          # nil
print(greet.type_of(true))         # bool

print(greet.add(1, 2))             # 3
print(greet.add(1, 2.5))           # 3.5

s = greet.summary("Alice", 85)
print(s["name"])                   # Alice
print(s["score"])                  # 85
print(s["passed"])                 # true
print(s["tags"][0])                # student
