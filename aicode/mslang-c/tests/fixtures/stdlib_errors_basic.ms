import "errors"

// errors.new + errors.message
var ErrNotFound = errors.new("not found")
print(errors.message(ErrNotFound))

// errors.wrap + errors.unwrap
var wrapped = errors.wrap(ErrNotFound, "read_file: empty path")
print(errors.message(wrapped))
print(errors.message(errors.unwrap(wrapped)))

// errors.is
print(errors.is(wrapped, ErrNotFound))
print(errors.is(wrapped, wrapped))

// errors.format
print(errors.format(wrapped))

// errors.chain
var chain = errors.chain(wrapped)
print(len(chain))

// throw / catch
fun read_file(path) {
    if (path == "") {
        throw errors.wrap(ErrNotFound, "read_file: empty path")
    }
}

try {
    read_file("")
} catch (e) {
    print(errors.message(e))
    print(errors.is(e, ErrNotFound))
    print(errors.format(e))
    var ch = errors.chain(e)
    print(len(ch))
}
