// purpose : integer arithmetic + tight loop (quickening OP_ADD_II / OP_LT_II)
// hot-op  : OP_ADD_II, OP_LT_II, OP_LOOP
// expected: 449999985000000
var N = 30000000
var sum = 0
for (var i = 0; i < N; i = i + 1) {
    sum = sum + i
}
print(sum)
