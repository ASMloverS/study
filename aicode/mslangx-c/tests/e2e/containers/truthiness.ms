if ("") {
  print "bad-empty-string"
} else {
  print "empty-string-false"
}

if ("ms") {
  print "string-true"
} else {
  print "bad-string"
}

if (empty_list()) {
  print "bad-empty-list"
} else {
  print "empty-list-false"
}

if (filled_list()) {
  print "list-true"
} else {
  print "bad-list"
}

if (empty_tuple()) {
  print "bad-empty-tuple"
} else {
  print "empty-tuple-false"
}

if (filled_tuple()) {
  print "tuple-true"
} else {
  print "bad-tuple"
}

if (empty_map()) {
  print "bad-empty-map"
} else {
  print "empty-map-false"
}

if (filled_map()) {
  print "map-true"
} else {
  print "bad-map"
}
