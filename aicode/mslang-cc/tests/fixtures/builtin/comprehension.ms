var xs = [1, 2, 3, 4, 5, 6]
print([x * x for x in xs if x % 2 == 0])
print({x % 3 for x in xs})
print(sorted({x % 3 for x in xs}))
var m = {"a": [1], "b": [2, 3]}
print({k: len(v) for k, v in m.items()})
var flat = [x + y for x in [1, 2] for y in [10, 20]]
print(flat)
print([x * x for x in range(1, 6)])
