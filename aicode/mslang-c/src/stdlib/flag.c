/* src/stdlib/flag.c - STDLIB-39: flag module
 *
 * Command-line argument parser, pure .ms implementation.
 *
 * Public API:
 *   flag.string(name, default, usage)  -> Ref  (FlagEntry instance)
 *   flag.int(name, default, usage)     -> Ref
 *   flag.float(name, default, usage)   -> Ref
 *   flag.bool(name, default, usage)    -> Ref
 *   flag.parse()                       -> nil  (reads os.argv()[1:])
 *   flag.parse_args(args)              -> nil
 *   flag.args()                        -> list (positional args after parse)
 *   flag.usage()                       -> str
 *
 * Ref (FlagEntry) fields/methods:
 *   ref.value    -- parsed value (default before parse())
 *   ref.is_set() -- bool, true if explicitly set on command line
 */
#include "ms/vm.h"
#include "ms/compiler.h"

/* All definitions are wrapped in a factory so that FlagEntry is captured
 * as an upvalue in every closure, matching the pattern used by ring.c. */
static const char FLAG_SOURCE[] =
    "var _make_flag_module = fun() {\n"

    /* FlagEntry: one registered flag */
    "    class FlagEntry {\n"
    "        init(name, ftype, dflt, usage) {\n"
    "            this.name  = name\n"
    "            this.ftype = ftype\n"
    "            this.value = dflt\n"
    "            this.dflt  = dflt\n"
    "            this.usage = usage\n"
    "            this._set  = false\n"
    "        }\n"
    "        mark_set(v) {\n"
    "            this.value = v\n"
    "            this._set  = true\n"
    "        }\n"
    "        is_set() {\n"
    "            return this._set\n"
    "        }\n"
    "    }\n"

    /* module state: closed over by all returned fns */
    "    var _entries  = []\n"
    "    var _pos_args = []\n"

    /* _find(name) */
    "    var _find = fun(name) {\n"
    "        var i = 0\n"
    "        while (i < _entries.len()) {\n"
    "            if (_entries[i].name == name) { return _entries[i] }\n"
    "            i = i + 1\n"
    "        }\n"
    "        return nil\n"
    "    }\n"

    /* _coerce(entry, raw) */
    "    var _coerce = fun(entry, raw) {\n"
    "        var t = entry.ftype\n"
    "        if (t == \"string\") { return raw }\n"
    "        if (t == \"bool\") {\n"
    "            if (raw == \"true\"  or raw == \"1\" or raw == \"yes\") { return true  }\n"
    "            if (raw == \"false\" or raw == \"0\" or raw == \"no\")  { return false }\n"
    "            error(\"flag: invalid bool '\" + raw + \"' for --\" + entry.name)\n"
    "        }\n"
    "        if (t == \"int\") {\n"
    "            import \"strconv\"\n"
    "            return strconv.parse_int(raw, 10)\n"
    "        }\n"
    "        if (t == \"float\") {\n"
    "            import \"strconv\"\n"
    "            return strconv.parse_float(raw)\n"
    "        }\n"
    "        return raw\n"
    "    }\n"

    /* _parse_args_fn(args) */
    "    var _parse_args_fn = fun(args) {\n"
    "        _pos_args = []\n"
    "        var i = 0\n"
    "        var n = args.len()\n"
    "        while (i < n) {\n"
    "            var tok = args[i]\n"
    "            if (tok == \"--\") {\n"
    "                i = i + 1\n"
    "                while (i < n) { _pos_args.push(args[i]) ; i = i + 1 }\n"
    "                return nil\n"
    "            }\n"
    "            var is_flag = (tok.len() > 1 and tok[0] == \"-\")\n"
    "            if (not is_flag) {\n"
    "                _pos_args.push(tok)\n"
    "                i = i + 1\n"
    "            } else {\n"
    "                var raw = tok\n"
    "                if (raw.len() > 1 and raw[0] == \"-\" and raw[1] == \"-\") {\n"
    "                    raw = raw.slice(2, raw.len())\n"
    "                } else {\n"
    "                    raw = raw.slice(1, raw.len())\n"
    "                }\n"
    "                var is_no = false\n"
    "                if (raw.len() > 3 and raw.slice(0, 3) == \"no-\") {\n"
    "                    is_no = true\n"
    "                    raw = raw.slice(3, raw.len())\n"
    "                }\n"
    "                var fname = raw\n"
    "                var fval  = nil\n"
    "                var ei = 0\n"
    "                while (ei < raw.len()) {\n"
    "                    if (raw[ei] == \"=\") {\n"
    "                        fname = raw.slice(0, ei)\n"
    "                        fval  = raw.slice(ei + 1, raw.len())\n"
    "                        ei = raw.len()\n"
    "                    }\n"
    "                    ei = ei + 1\n"
    "                }\n"
    "                var entry = _find(fname)\n"
    "                if (entry == nil) {\n"
    "                    error(\"flag: unknown flag --\" + fname)\n"
    "                }\n"
    "                if (entry.ftype == \"bool\") {\n"
    "                    if (is_no) {\n"
    "                        entry.mark_set(false)\n"
    "                    } else if (fval == nil) {\n"
    "                        entry.mark_set(true)\n"
    "                    } else {\n"
    "                        entry.mark_set(_coerce(entry, fval))\n"
    "                    }\n"
    "                } else {\n"
    "                    if (fval == nil) {\n"
    "                        i = i + 1\n"
    "                        if (i >= n) { error(\"flag: missing value for --\" + fname) }\n"
    "                        fval = args[i]\n"
    "                    }\n"
    "                    entry.mark_set(_coerce(entry, fval))\n"
    "                }\n"
    "                i = i + 1\n"
    "            }\n"
    "        }\n"
    "        return nil\n"
    "    }\n"

    /* exported functions */
    "    var mod = {}\n"
    "    mod[\"string\"] = fun(name, dflt, usage) {\n"
    "        var e = FlagEntry(name, \"string\", dflt, usage)\n"
    "        _entries.push(e)\n"
    "        return e\n"
    "    }\n"
    "    mod[\"int\"] = fun(name, dflt, usage) {\n"
    "        var e = FlagEntry(name, \"int\", dflt, usage)\n"
    "        _entries.push(e)\n"
    "        return e\n"
    "    }\n"
    "    mod[\"float\"] = fun(name, dflt, usage) {\n"
    "        var e = FlagEntry(name, \"float\", dflt, usage)\n"
    "        _entries.push(e)\n"
    "        return e\n"
    "    }\n"
    "    mod[\"bool\"] = fun(name, dflt, usage) {\n"
    "        var e = FlagEntry(name, \"bool\", dflt, usage)\n"
    "        _entries.push(e)\n"
    "        return e\n"
    "    }\n"
    "    mod[\"parse_args\"] = _parse_args_fn\n"
    "    mod[\"parse\"] = fun() {\n"
    "        import \"os\"\n"
    "        var argv = os.argv()\n"
    "        var tail = argv.slice(1, argv.len())\n"
    "        return _parse_args_fn(tail)\n"
    "    }\n"
    "    mod[\"args\"] = fun() { return _pos_args }\n"
    "    mod[\"usage\"] = fun() {\n"
    "        var out = \"Flags:\\n\"\n"
    "        var i = 0\n"
    "        while (i < _entries.len()) {\n"
    "            var e = _entries[i]\n"
    "            out = out + \"  --\" + e.name + \"  \" + e.usage + \" (default: \" + e.dflt + \")\\n\"\n"
    "            i = i + 1\n"
    "        }\n"
    "        return out\n"
    "    }\n"
    "    return mod\n"
    "}\n"
    "var _fm = _make_flag_module()\n"
    "var string     = _fm[\"string\"]\n"
    "var int        = _fm[\"int\"]\n"
    "var float      = _fm[\"float\"]\n"
    "var bool       = _fm[\"bool\"]\n"
    "var parse_args = _fm[\"parse_args\"]\n"
    "var parse      = _fm[\"parse\"]\n"
    "var args       = _fm[\"args\"]\n"
    "var usage      = _fm[\"usage\"]\n";

void ms_module_flag_init(MsVM* vm, MsObjModule* mod) {
    MsDiagnostic diags[8];
    int diag_count = 0;
    MsObjFunction* fn = ms_compile(vm, FLAG_SOURCE, "<builtin:flag>",
                                   diags, &diag_count, 8);
    if (!fn) {
        if (diag_count > 0)
            ms_vm_runtime_error(vm, "flag: compile error: %s",
                                diags[0].message);
        else
            ms_vm_runtime_error(vm, "flag: compile failed");
        return;
    }
    ms_vm_execute_module(vm, fn, mod);
}
