#include <ctpy.hpp>

// M7 checkpoint: f-strings. The grammar kept each f-string body
// verbatim in one fstr_lit node; fstring.hpp scans it into literal
// segments and {expr} holes and re-runs every hole through the SAME
// Tablewright expression grammar the rest of the script used. Hole
// values format with str() (not repr), {{ and }} are literal braces,
// format specs/conversions are out of v0.1 (hard errors, not testable
// here). Every expectation is CPython-verified.

using ctpy::run;
using ctpy::eval;

// --- plain bodies and brace escapes -------------------------------------------

static_assert(eval<"f''">().str() == "");
static_assert(eval<"f'plain'">().str() == "plain");
static_assert(eval<R"(f"{{x}}")">().str() == "{x}");
static_assert(eval<R"(f"a}}b{{c")">().str() == "a}b{c");
static_assert(eval<R"(f"{{{1 + 1}}}")">().str() == "{2}"); // {{ then a hole then }}

// backslash escapes cook in literal segments, exactly like plain strs
static_assert(eval<R"(f"a\tb\n")">().str() == "a\tb\n");

// --- holes: expressions at the same grammar level ---------------------------------

static_assert(eval<"f'{1 + 1}'">().str() == "2");
static_assert(eval<R"(f"a{1 + 1}b")">().str() == "a2b"); // adjacent text both sides
static_assert(eval<"f'{2 ** 10}'">().str() == "1024");
static_assert(eval<"f'{1 / 2}'">().str() == "0.5");
static_assert(eval<"f'{-7}'">().str() == "-7");
static_assert(eval<"f'{True} {None}'">().str() == "True None");
static_assert(eval<"f'{ 42 }'">().str() == "42"); // blanks around the hole trim
static_assert(eval<"f'{1 if 0 else 2}'">().str() == "2"); // any expression works
static_assert(eval<"f'{max(1, 2)}'">().str() == "2");      // calls too
static_assert(eval<"f'{(1, 2)[0]}'">().str() == "1");

// adjacent holes, evaluated left to right
static_assert(eval<"f'{1}{2}'">().str() == "12");
static_assert(run<"x = 3\ns = f'{x}{x + 1}{x * 2}'\n">()["s"].str() == "346");

// str() conversion, not repr: strs interpolate unquoted...
static_assert(eval<"f\"{'s'}\"">().str() == "s");
static_assert(eval<"f\"{'ab' * 2}!\"">().str() == "abab!");
// ...but containers repr their string elements (str() of the container)
static_assert(eval<"f\"{[1, 'a']}\"">().str() == "[1, 'a']");
static_assert(eval<"f'{range(3)}'">().str() == "range(0, 3)");

// quotes and brackets INSIDE a hole do not close it
static_assert(run<"d = {'k': 5}\ns = f'{d[\"k\"]}'\n">()["s"].str() == "5");
static_assert(eval<"f\"{'nested {braces}' + '!'}\"">().str() == "nested {braces}!");
static_assert(eval<"f'{ {1: 2}[1] }'">().str() == "2"); // a dict display in a hole
static_assert(eval<"f'{1 != 2}'">().str() == "True");   // '!=' is not a conversion

// a nested f-string in the other quote kind
static_assert(eval<"f\"{f'{1 + 1}'}\"">().str() == "2");

// --- f-strings inside programs ------------------------------------------------------

static_assert(run<"x = 7\ny = f'x={x}!'\n">()["y"].str() == "x=7!");
static_assert(run<"print(f'v={2 ** 3}')\n">().stdout() == "v=8\n");
static_assert(run<
"total = 0\n"
"for i in range(4):\n"
"    total += i\n"
"print(f'total is {total}')\n">().stdout() == "total is 6\n");
static_assert(run<
"def greet(name):\n"
"    return f'hello, {name}!'\n"
"\n"
"print(greet('world'))\n">().stdout() == "hello, world!\n");

// names resolve in the CURRENT scope, per Python
static_assert(run<
"x = 'global'\n"
"def f():\n"
"    x = 'local'\n"
"    return f'{x}'\n"
"\n"
"y = f()\n">()["y"].str() == "local");

// --- the soft channel threads through holes --------------------------------------------

static_assert(run<"s = f'{1 // 0}'\n">().exception() == ctpy::ZeroDivisionError);
static_assert(!run<"s = f'{1 // 0}'\n">().ok());
static_assert(run<"s = f'{nope}'\n">().exception() == ctpy::NameError);
static_assert(run<"s = f'{nope}'\n">().exception().message() == "name 'nope' is not defined");
// the first raising hole wins; later holes never evaluate
static_assert(run<"s = f'{1 // 0}{nope}'\n">().exception() == ctpy::ZeroDivisionError);

int main() { }
