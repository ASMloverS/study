import "bufops"

print(bufops.to_upper("hello"))      # HELLO
print(bufops.to_upper("mslang-c!")) # MSLANG-C!

print(bufops.hex_encode("abc"))      # 616263
print(bufops.hex_encode("Hi!"))      # 486921

print(bufops.byte_count("hello"))    # 5
print(bufops.byte_count(""))         # 0
