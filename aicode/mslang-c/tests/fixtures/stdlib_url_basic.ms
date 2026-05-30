import "url"

// encode / decode
print(url.encode("hello world!"))
// expect: hello%20world%21
print(url.decode("hello%20world"))
// expect: hello world
print(url.encode_path("/foo bar/baz"))
// expect: /foo%20bar/baz
print(url.encode_query("hello world"))
// expect: hello+world

// parse
var u = url.parse("https://user:pass@example.com:8080/path?a=1&b=2#frag")
print(u.scheme)
// expect: https
print(u.host)
// expect: example.com
print(u.port)
// expect: 8080
print(u.path)
// expect: /path
print(u.fragment)
// expect: frag
print(u.userinfo)
// expect: user:pass

// query_params
var m = u.query_params()
print(m["a"])
// expect: 1
print(m["b"])
// expect: 2

// build
print(url.build("https", "example.com", "/path", nil, nil))
// expect: https://example.com/path

// build_query
var q = url.build_query({"x": "1", "y": "2"})
print(q.len() > 0)
// expect: true

// try_parse
print(url.try_parse("not a url") == nil)
// expect: true
