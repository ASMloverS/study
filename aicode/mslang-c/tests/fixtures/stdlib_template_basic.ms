import "template"

// Basic variable substitution
print(template.render("Hello, {{name}}!", {"name": "Alice"}))
// expect: Hello, Alice!

// Missing key → empty string
print(template.render("[{{x}}]", {}))
// expect: []

// Escape \\{{ → literal {{ (in .ms source \\{{ becomes \{{ for template engine)
print(template.render("price: \\{{amount}}", {}))
// expect: price: {{amount}}

// Comment tag
print(template.render("a{{! ignored }}b", {}))
// expect: ab

// if/end (true)
print(template.render("{{if ok}}yes{{end}}", {"ok": true}))
// expect: yes

// if/end (false) - produces empty string
print(template.render("{{if ok}}yes{{end}}", {"ok": false}) == "")
// expect: true

// if/else/end
print(template.render("{{if admin}}admin{{else}}user{{end}}", {"admin": true}))
// expect: admin
print(template.render("{{if admin}}admin{{else}}user{{end}}", {"admin": false}))
// expect: user

// for loop over list
print(template.render("{{for item in items}}{{item}} {{end}}", {"items": ["a", "b", "c"]}))
// expect: a b c

// Numeric values
print(template.render("{{n}}", {"n": 42}))
// expect: 42

// Compiled template reuse
var tmpl = template.parse("Hi, {{who}}!")
print(template.render_tmpl(tmpl, {"who": "Bob"}))
// expect: Hi, Bob!
print(template.render_tmpl(tmpl, {"who": "Carol"}))
// expect: Hi, Carol!

// Full example from spec
var full = template.parse(
    "Hello, {{name}}!\n" +
    "{{if admin}}You are an admin.{{else}}You are a user.{{end}}\n" +
    "Your scores:\n" +
    "{{for score in scores}}" +
    "  - {{score}}\n" +
    "{{end}}"
)
var out = template.render_tmpl(full, {
    "name":   "Alice",
    "admin":  true,
    "scores": [95, 87, 92]
})
print(out)
// expect: Hello, Alice!
// expect: You are an admin.
// expect: Your scores:
// expect:   - 95
// expect:   - 87
// expect:   - 92

print("template_ok")
// expect: template_ok
