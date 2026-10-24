// T013 索引/切片：rune 语义（docs/tasks/T013 §5.2；02 §4）
var s = "Hello, 世界"
print(s[0])            // 72
print(s[7])            // 19990
print(s[0:5])          // Hello
print(s[::-1])         // 界世 ,olleH
print(len(s))          // 13
print(s.charLen())     // 9
