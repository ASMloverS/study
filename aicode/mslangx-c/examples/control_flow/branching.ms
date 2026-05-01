var total = 0

for (var i = 0; i < 6; i = i + 1) {
  if (i == 1) continue
  if (i == 4) break
  total = total + i
}

print total

var branch = ""
if (total > 5) {
  branch = "big"
} else {
  branch = "small"
}
print branch

var fallback = false or "fallback"
var kept = true and "kept"
print fallback
print kept
