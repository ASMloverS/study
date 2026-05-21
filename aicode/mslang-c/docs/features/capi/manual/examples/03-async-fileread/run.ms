import "fileread"

contents = fileread.read_sync(
    "docs/features/capi/manual/examples/03-async-fileread/sample.txt"
)
print(contents)
