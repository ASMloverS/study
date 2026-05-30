import "flag"

// parse_args directly (avoids depending on os.argv in test)
var host    = flag.string("host", "localhost", "server address")
var port    = flag.int("port", 8080, "listen port")
var verbose = flag.bool("verbose", false, "verbose output")

flag.parse_args(["--host", "127.0.0.1", "--port", "9090", "--verbose", "extra"])

print(host.value)
// expect: 127.0.0.1
print(port.value)
// expect: 9090
print(verbose.value)
// expect: true
print(flag.args())
// expect: [extra]
print(host.is_set())
// expect: true
