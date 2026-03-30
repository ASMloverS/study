var total = 0
for (var i = 0; i < 5; i = i + 1) {
  if (i == 1) continue
  if (i == 4) break
  total = total + i
}
print total
