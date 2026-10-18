try {
    risky()
} catch ValueError, TypeError {
    print("typed")
} catch e {
    print(e)
} finally {
    cleanup()
}
