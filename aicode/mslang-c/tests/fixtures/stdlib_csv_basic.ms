import "csv"

// read_str basic
var rows = csv.read_str("a,b,c\n1,2,3\n4,5,6\n")
print(rows.len())
// expect: 3
print(rows[0][0])
// expect: a
print(rows[1][2])
// expect: 3
print(rows[2][1])
// expect: 5

// read_str with quoted field
var qrows = csv.read_str("\"hello world\",ok\n")
print(qrows[0][0])
// expect: hello world
print(qrows[0][1])
// expect: ok

// read_str custom separator
var trows = csv.read_str("x;y;z\n", ";")
print(trows[0][0])
// expect: x
print(trows[0][2])
// expect: z

// write_str basic
var s = csv.write_str([["name","age"],["Alice","30"]])
print(s)
// expect: name,age
// expect: Alice,30

// write_str: separator in field → quoted
var sq = csv.write_str([["a,b","c"]])
print(sq)
// expect: "a,b",c

// round-trip
var orig = [["id","val"],["1","hello"],["2","world"]]
var csv_str = csv.write_str(orig)
var back = csv.read_str(csv_str)
print(back.len())
// expect: 3
print(back[0][0])
// expect: id
print(back[2][1])
// expect: world
