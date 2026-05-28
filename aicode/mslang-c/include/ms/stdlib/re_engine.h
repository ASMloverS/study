#ifndef MS_RE_ENGINE_H
#define MS_RE_ENGINE_H

#include <stddef.h>
#include <stdbool.h>

/* Maximum number of capture groups (excluding group 0 = whole match). */
#define RE_MAX_GROUPS 16

/* ---- NFA state ---- */
typedef struct ReState ReState;
typedef struct ReNfa   ReNfa;

/* ---- Compiled regex ---- */
struct ReNfa {
    ReState* states;     /* pool */
    int      count;      /* number of states allocated */
    int      start_id;   /* start state index */
    int      end_id;     /* accept state index */
    int      n_groups;   /* number of capture groups */
    bool     flag_i;     /* case-insensitive */
    bool     flag_m;     /* multiline (^ and $ match line boundaries) */
};

/* ---- Submatch ---- */
typedef struct {
    const char* start;
    const char* end;     /* one past last char; NULL if group not matched */
} ReMatch;

/* ---- API ---- */

/* Compile pattern.  Returns NULL and sets err_out on failure.
   err_out must be a buffer of at least 128 bytes. */
ReNfa* re_compile(const char* pattern, char* err_out);

/* Free compiled NFA (internals + struct). */
void re_free(ReNfa* nfa);

/* Free NFA internals only, NOT the nfa struct itself.
   Used when ud->data = nfa and object.c frees nfa via free(ud->data). */
void re_free_contents(ReNfa* nfa);

/* Try to match from the start of s (anchor = match semantics).
   groups[0] = whole match.  Returns true on success. */
bool re_match(const ReNfa* nfa, const char* s, size_t len,
              ReMatch groups[RE_MAX_GROUPS]);

/* Search anywhere in s[0..len).
   groups[0] = whole match.  Returns true on success. */
bool re_search(const ReNfa* nfa, const char* s, size_t len,
               ReMatch groups[RE_MAX_GROUPS]);

/* Search starting from s+offset.  Fills groups.  Returns true on success. */
bool re_search_at(const ReNfa* nfa, const char* s, size_t len, size_t offset,
                  ReMatch groups[RE_MAX_GROUPS]);

#endif /* MS_RE_ENGINE_H */
