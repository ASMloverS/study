import cache.left
import cache.right

__gc_collect__()

print left.label
print right.label
print left.counter == right.counter
