/* src/stdlib/url.c - STDLIB-40: url module
 *
 * C natives: encode, encode_path, encode_query, decode
 * .ms layer:  URL class, parse, try_parse, build, build_query
 *
 * Public API:
 *   url.encode(s)                               → str
 *   url.encode_path(s)                          → str  (preserves /)
 *   url.encode_query(s)                         → str  (space→+)
 *   url.decode(s)                               → str
 *   url.parse(s)                                → URL  (throws on bad URL)
 *   url.try_parse(s)                            → URL|nil
 *   url.build(scheme, host, path, query, frag)  → str
 *   url.build_query(params_map)                 → str
 *
 * URL instance fields: scheme host port path raw_query fragment userinfo
 * URL instance methods: query_params() to_str() join(ref)
 */
#include "ms/module.h"
#include "ms/vm.h"
#include "ms/compiler.h"
#include "ms/object.h"
#include "ms/value.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static bool is_unreserved(unsigned char c) {
    return isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
}

static int hex_nibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Write percent-encoded byte to buf (must have space for 3 chars). */
static void pct_encode(char* buf, unsigned char byte) {
    static const char HEX[] = "0123456789ABCDEF";
    buf[0] = '%';
    buf[1] = HEX[(byte >> 4) & 0xF];
    buf[2] = HEX[byte & 0xF];
}

/* Generic encode: pass_slash controls whether '/' passes through.
   query_mode: space → '+'. */
static MsValue encode_impl(MsVM* vm, const char* src, int len,
                           bool pass_slash, bool query_mode) {
    /* worst case: every byte → 3 chars */
    char* out = (char*)malloc((size_t)len * 3 + 1);
    if (!out) { ms_vm_runtime_error(vm, "url.encode: OOM"); return MS_NIL_VAL(); }
    int pos = 0;
    for (int i = 0; i < len; i++) {
        unsigned char c = (unsigned char)src[i];
        if (is_unreserved(c)) {
            out[pos++] = (char)c;
        } else if (pass_slash && c == '/') {
            out[pos++] = '/';
        } else if (query_mode && c == ' ') {
            out[pos++] = '+';
        } else {
            pct_encode(out + pos, c);
            pos += 3;
        }
    }
    out[pos] = '\0';
    MsValue result = MS_OBJ_VAL(ms_obj_string_copy(vm, out, pos));
    free(out);
    return result;
}

/* ------------------------------------------------------------------ */
/* url.encode(s) → str                                                 */
/* ------------------------------------------------------------------ */

static MsValue url_encode(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "url.encode: expected string");
        return MS_NIL_VAL();
    }
    MsObjString* s = MS_AS_STRING(argv[0]);
    return encode_impl(vm, s->data, s->length, false, false);
}

/* ------------------------------------------------------------------ */
/* url.encode_path(s) → str  (preserves /)                            */
/* ------------------------------------------------------------------ */

static MsValue url_encode_path(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "url.encode_path: expected string");
        return MS_NIL_VAL();
    }
    MsObjString* s = MS_AS_STRING(argv[0]);
    return encode_impl(vm, s->data, s->length, true, false);
}

/* ------------------------------------------------------------------ */
/* url.encode_query(s) → str  (space→+)                               */
/* ------------------------------------------------------------------ */

static MsValue url_encode_query(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "url.encode_query: expected string");
        return MS_NIL_VAL();
    }
    MsObjString* s = MS_AS_STRING(argv[0]);
    return encode_impl(vm, s->data, s->length, false, true);
}

/* ------------------------------------------------------------------ */
/* url.decode(s) → str                                                 */
/* ------------------------------------------------------------------ */

static MsValue url_decode(MsVM* vm, int argc, MsValue* argv) {
    (void)argc;
    if (!MS_IS_STRING(argv[0])) {
        ms_vm_runtime_error(vm, "url.decode: expected string");
        return MS_NIL_VAL();
    }
    MsObjString* s = MS_AS_STRING(argv[0]);
    const char* src = s->data;
    int len = s->length;
    char* out = (char*)malloc((size_t)len + 1);
    if (!out) { ms_vm_runtime_error(vm, "url.decode: OOM"); return MS_NIL_VAL(); }
    int pos = 0;
    for (int i = 0; i < len; ) {
        if (src[i] == '%' && i + 2 < len) {
            int hi = hex_nibble(src[i + 1]);
            int lo = hex_nibble(src[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out[pos++] = (char)((hi << 4) | lo);
                i += 3;
                continue;
            }
        }
        if (src[i] == '+') {
            out[pos++] = ' ';
            i++;
            continue;
        }
        out[pos++] = src[i++];
    }
    out[pos] = '\0';
    MsValue result = MS_OBJ_VAL(ms_obj_string_copy(vm, out, pos));
    free(out);
    return result;
}

/* ------------------------------------------------------------------ */
/* .ms source: URL class + parse + build                               */
/* ------------------------------------------------------------------ */

static const char URL_SOURCE[] =
    "var _make_url_module = fun() {\n"

    /* URL class */
    "    class URL {\n"
    "        init(scheme, host, port, path, raw_query, fragment, userinfo) {\n"
    "            this.scheme     = scheme\n"
    "            this.host       = host\n"
    "            this.port       = port\n"
    "            this.path       = path\n"
    "            this.raw_query  = raw_query\n"
    "            this.fragment   = fragment\n"
    "            this.userinfo   = userinfo\n"
    "        }\n"

    "        query_params() {\n"
    "            var m = {}\n"
    "            if (this.raw_query == \"\") { return m }\n"
    "            import \"strings\"\n"
    "            var pairs = strings.split(this.raw_query, \"&\")\n"
    "            var i = 0\n"
    "            while (i < pairs.len()) {\n"
    "                var kv = strings.split(pairs[i], \"=\")\n"
    "                if (kv.len() >= 2) {\n"
    "                    m[kv[0]] = kv[1]\n"
    "                } else if (kv.len() == 1 and kv[0] != \"\") {\n"
    "                    m[kv[0]] = \"\"\n"
    "                }\n"
    "                i = i + 1\n"
    "            }\n"
    "            return m\n"
    "        }\n"

    "        to_str() {\n"
    "            var s = this.scheme + \"://\"\n"
    "            if (this.userinfo != \"\") {\n"
    "                s = s + this.userinfo + \"@\"\n"
    "            }\n"
    "            s = s + this.host\n"
    "            if (this.port != \"\") {\n"
    "                s = s + \":\" + this.port\n"
    "            }\n"
    "            s = s + this.path\n"
    "            if (this.raw_query != \"\") {\n"
    "                s = s + \"?\" + this.raw_query\n"
    "            }\n"
    "            if (this.fragment != \"\") {\n"
    "                s = s + \"#\" + this.fragment\n"
    "            }\n"
    "            return s\n"
    "        }\n"

    /* join: if ref has scheme:// treat as absolute, else resolve relative */
    "        join(ref) {\n"
    "            import \"strings\"\n"
    "            if (strings.index(ref, \"://\") >= 0) { return ref }\n"
    "            var base = this.scheme + \"://\" + this.host\n"
    "            if (this.port != \"\") { base = base + \":\" + this.port }\n"
    "            var dir = this.path\n"
    "            var last_slash = 0\n"
    "            var ii = 0\n"
    "            while (ii < dir.len()) {\n"
    "                if (dir[ii] == \"/\") { last_slash = ii }\n"
    "                ii = ii + 1\n"
    "            }\n"
    "            var parent = dir.slice(0, last_slash + 1)\n"
    "            return base + parent + ref\n"
    "        }\n"
    "    }\n"

    /* _do_parse(s) -> URL or nil  (closed over URL class via upvalue) */
    "    var _do_parse = fun(s) {\n"
    "        import \"strings\"\n"
    /* extract fragment */
    "        var fragment = \"\"\n"
    "        var frag_idx = strings.last_index(s, \"#\")\n"
    "        if (frag_idx >= 0) {\n"
    "            fragment = s.slice(frag_idx + 1, s.len())\n"
    "            s = s.slice(0, frag_idx)\n"
    "        }\n"
    /* extract query */
    "        var raw_query = \"\"\n"
    "        var q_idx = strings.index(s, \"?\")\n"
    "        if (q_idx >= 0) {\n"
    "            raw_query = s.slice(q_idx + 1, s.len())\n"
    "            s = s.slice(0, q_idx)\n"
    "        }\n"
    /* extract scheme */
    "        var scheme_end = strings.index(s, \"://\")\n"
    "        if (scheme_end < 0) { return nil }\n"
    "        var scheme = s.slice(0, scheme_end)\n"
    "        var rest = s.slice(scheme_end + 3, s.len())\n"
    /* extract path */
    "        var path = \"\"\n"
    "        var slash_idx = strings.index(rest, \"/\")\n"
    "        var authority = rest\n"
    "        if (slash_idx >= 0) {\n"
    "            authority = rest.slice(0, slash_idx)\n"
    "            path = rest.slice(slash_idx, rest.len())\n"
    "        }\n"
    /* extract userinfo */
    "        var userinfo = \"\"\n"
    "        var at_idx = strings.index(authority, \"@\")\n"
    "        if (at_idx >= 0) {\n"
    "            userinfo = authority.slice(0, at_idx)\n"
    "            authority = authority.slice(at_idx + 1, authority.len())\n"
    "        }\n"
    /* extract host and port */
    "        var host = authority\n"
    "        var port = \"\"\n"
    "        var colon_idx = strings.index(authority, \":\")\n"
    "        if (colon_idx >= 0) {\n"
    "            host = authority.slice(0, colon_idx)\n"
    "            port = authority.slice(colon_idx + 1, authority.len())\n"
    "        }\n"
    "        return URL(scheme, host, port, path, raw_query, fragment, userinfo)\n"
    "    }\n"

    "    var mod = {}\n"

    "    mod[\"parse\"] = fun(s) {\n"
    "        var u = _do_parse(s)\n"
    "        if (u == nil) { error(\"url.parse: invalid URL: \" + s) }\n"
    "        return u\n"
    "    }\n"

    "    mod[\"try_parse\"] = fun(s) {\n"
    "        return _do_parse(s)\n"
    "    }\n"

    "    mod[\"build\"] = fun(scheme, host, path, query, fragment) {\n"
    "        var s = scheme + \"://\" + host + path\n"
    "        if (query != nil and query != \"\") { s = s + \"?\" + query }\n"
    "        if (fragment != nil and fragment != \"\") { s = s + \"#\" + fragment }\n"
    "        return s\n"
    "    }\n"

    "    mod[\"build_query\"] = fun(params) {\n"
    "        import \"strings\"\n"
    "        var parts = []\n"
    "        var keys = params.keys()\n"
    "        var i = 0\n"
    "        while (i < keys.len()) {\n"
    "            var k = keys[i]\n"
    "            parts.push(k + \"=\" + params[k])\n"
    "            i = i + 1\n"
    "        }\n"
    "        return strings.join(parts, \"&\")\n"
    "    }\n"

    "    return mod\n"
    "}\n"
    "var _um = _make_url_module()\n"
    "var parse       = _um[\"parse\"]\n"
    "var try_parse   = _um[\"try_parse\"]\n"
    "var build       = _um[\"build\"]\n"
    "var build_query = _um[\"build_query\"]\n";

/* ------------------------------------------------------------------ */
/* Native defs                                                          */
/* ------------------------------------------------------------------ */

static const MsNativeDef url_natives[] = {
    {"encode",       url_encode,       1},
    {"encode_path",  url_encode_path,  1},
    {"encode_query", url_encode_query, 1},
    {"decode",       url_decode,       1},
    {NULL, NULL, 0}
};

void ms_module_url_init(MsVM* vm, MsObjModule* mod) {
    ms_module_register_natives(vm, mod, url_natives);

    MsDiagnostic diags[8];
    int diag_count = 0;
    MsObjFunction* fn = ms_compile(vm, URL_SOURCE, "<builtin:url>",
                                   diags, &diag_count, 8);
    if (!fn) {
        if (diag_count > 0)
            ms_vm_runtime_error(vm, "url: compile error: %s", diags[0].message);
        else
            ms_vm_runtime_error(vm, "url: compile failed");
        return;
    }
    ms_vm_execute_module(vm, fn, mod);
}
