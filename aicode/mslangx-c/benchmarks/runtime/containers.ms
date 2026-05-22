var values = []
for (var i = 0; i < 2000; i = i + 1) {
  values = [i, i + 1, i + 2, i + 3, i + 4, i + 5]
}

var names = {"alpha": 1, "beta": 2, "gamma": 3, "delta": 4}
for (var j = 0; j < 2000; j = j + 1) {
  names["alpha"] = names["alpha"] + names["beta"]
}

print values[5]
print names["alpha"]
