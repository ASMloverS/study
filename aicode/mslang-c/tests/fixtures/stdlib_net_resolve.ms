import "net"

// listen on port 0 (OS assigns port), then close
var srv = net.listen("127.0.0.1", 0)
assert(srv != nil)
srv.close()

// listen with explicit backlog
var srv2 = net.listen("127.0.0.1", 0, 64)
assert(srv2 != nil)
srv2.close()

// net.close (module-level) is idempotent
var srv3 = net.listen("127.0.0.1", 0)
net.close(srv3)
net.close(srv3)

// tcp_pair returns list of 2 sockets
var pair = net.tcp_pair()
assert(len(pair) == 2)
pair[0].close()
pair[1].close()

print("ok")
// expect: ok
