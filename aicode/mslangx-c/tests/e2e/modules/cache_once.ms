import cache.counter as counter

__gc_collect__()

import cache.counter as counter_again

print counter == counter_again
print counter.value
print counter_again.value
