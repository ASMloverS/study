import state.counter
from state.counter import value as captured

print captured
print counter.bump()
print captured
print counter.value
