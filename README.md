# ctpy — compile-time Python

Run Python **while your C++ compiles**. A Python script is a string; ctpy
parses it and executes it in a `constexpr` tree-walking interpreter — `print` output,
globals, and Python exceptions all come back as compile-time values you can
`static_assert` on, persist to static storage, or lift into
[ctc](https://github.com/alexios-angel/compile-time-containers) containers.

```cpp
#include <ctpy.hpp>

constexpr auto out = ctpy::run<R"py(
def fib(n):
    a, b = 0, 1
    for _ in range(n):
        a, b = b, a + b
    return a

answer = fib(10)
print("fib(10) =", answer)
)py">();

static_assert(out.ok());                         // no Python exception
static_assert(out.stdout() == "fib(10) = 55\n"); // print() -> captured stdout
static_assert(out["answer"].to<int>() == 55);    // globals are readable
```

## The boundary: meta-embedded Python ↔ host C++

**Python → C++.** `run<src>()` returns a result: `ok()`, `exception()`
(a *raising* script does not fail your build — the traceback is a value),
`stdout()`, `operator[]` over the script's globals, and `.globals()`
iteration. Globals come back as `ctpy::value` — a uniform,
null-object-chaining view with `to<T>()` / `str()` conversion and
`begin()`/`end()` element iteration:

```cpp
constexpr auto cfg = ctpy::run<"cfg = {'name': 'ctpy', 'dims': [3, 4]}\n">();
static_assert(cfg["cfg"]["name"].str() == "ctpy"); // views chain to any depth
static_assert(cfg["cfg"]["dims"][1].to<int>() == 4);
static_assert(!cfg["cfg"]["nope"][0].exists());    // misses chain harmlessly
```

Whole structures lift into ctc containers (right-size with `ctc::shrunk`
as usual):

```cpp
constexpr auto sorted_out = ctpy::run<"xs = sorted([3, 1, 4, 1, 5])\n">();
constexpr auto xs = ctpy::lift<ctc::vector<int, 16>>(sorted_out["xs"]);
static_assert(xs.size() == 5 && xs[0] == 1 && xs[4] == 5);
```

**C++ → Python.** Named arguments seed the script's globals — scalars,
string literals, and ctc containers (`vector`/`map`/`string`, nested) all
lift in:

```cpp
constexpr auto r = ctpy::run<"total = sum(values) * factor\n">(
        ctpy::arg<"values">(ctc::vector<int, 3>{1, 2, 3}),
        ctpy::arg<"factor">(10));
static_assert(r["total"].to<int>() == 60);
```

**Sugar.** `ctpy::eval<"2 ** 10">().to<int>()` evaluates one expression;
`ctpy::module<src>` parses once and `.call<"fn">(args...)` invokes a `def`
(arguments lift like `arg<>` payloads, the return value rides the result):

```cpp
constexpr auto fib = ctpy::module<R"py(
def fib(n):
    a, b = 0, 1
    for _ in range(n):
        a, b = b, a + b
    return a
)py">;
static_assert(fib.call<"fib">(20).to<int>() == 6765);
```

**Failure policy** (family convention): a script that fails to *parse*
hard-errors the build with a static_assert naming the failing stage (prelex
or parse). The soft path never hard-errors: `ctpy::is_valid<src>` is a plain
bool, `ctpy::error_info<src>()` reports structured fields (stage, kind,
position, line, column), and `ctpy::error_message<src>()` renders a caret
diagnostic into static storage:

```text
ctpy: parse error (SyntaxError) at line 1, column 8: invalid syntax
  y = 1 +
         ^
```

A script that *raises* is a value: `out.ok()` is false and `out.exception()`
compares against `ctpy::ZeroDivisionError`, `ctpy::TypeError`, ... with the
exact CPython message — and the source line of the raising statement — 
queryable:

```cpp
constexpr auto boom = ctpy::run<"x = 1\ny = x // 0\n">();
static_assert(!boom.ok());
static_assert(boom.exception() == ctpy::ZeroDivisionError);
static_assert(boom.exception().message() == "division by zero");
static_assert(boom.exception().line == 2);  // like a traceback would say
```

**IO today, `std::embed` tomorrow.** There is no filesystem at compile time
(yet), so `open()` raises `OSError` — unless you mount one:

```cpp
constexpr auto io = ctpy::run<R"py(
data = open("config.txt").read()
)py">(ctpy::file<"config.txt", "timeout=250\n">);
static_assert(io["data"].str() == "timeout=250\n");
```

When `std::embed` lands, `ctpy::file<"path">` gains a real-file overload and
nothing else changes. `input()` reads the mounted `ctpy::stdin_text<"...">`
line by line:

```cpp
constexpr auto fed = ctpy::run<"who = input()\n">(ctpy::stdin_text<"world\n">);
static_assert(fed["who"].str() == "world");
```

**The stdlib seam.** `import` resolves against a compile-time module
registry; `ctpy::pymodule<"helpers", src>` mounts user modules (v0.1 fixes
the descriptor shape and validates the source parses — `import` execution is
deferred). Python's pure-Python standard library lands here incrementally in
future versions.

## Status

The v0.1 interpreter is complete and running: int, float, bool, None, str,
list, tuple, dict, set; assignment (incl. tuple unpacking, augmented,
chained); the full operator ladder (ternary, or/and/not, chained
comparisons, in/is, bitwise, shifts, arithmetic, unary, `**`); calls,
indexing, slicing, method attributes; if/elif/else, while/for
(+break/continue/else); def with default args and recursion (RecursionError
guard at depth 100, no closures); print (sep/end), len, range, sum, min,
max, abs, str, int, bool, sorted, enumerate, zip, open, input; f-strings
(`{expr}`, no format specs); the whole C++ boundary above — results
right-sized into per-script static storage, `ctpy::value` views, `arg<>`
in, `lift<>` out, the `file<>`/`stdin_text<>` IO seam; and the full
diagnostics story — `error_info<src>()` structured fields,
`error_message<src>()` caret rendering, and CPython-style line numbers on
every raised exception (`out.exception().line`, threaded from the
pre-lexer through per-statement AST line stamps). A checked-in CPython
parity suite (tests/parity.cpp, expectations generated by running real
python3) pins `run<src>().stdout()` against genuine CPython output.

Deferred beyond v0.1: classes, generators, comprehensions, try/except,
import execution, with, decorators, async. Documented deviations (ints are
64-bit — overflow raises where CPython grows a bignum; floats print with 16
significant digits; enumerate/zip materialize; calls are positional-only
except print; exception lines are per-statement, so an expression inside a
multi-line statement reports the statement's first line) live in PLAN.md's
progress notes. Still to come for v0.1: the release scaffolding (CI,
examples, single-header).

## Build & test

Compiling the tests IS the test — `tests/*.cpp` are `static_assert` suites.

```bash
make CXX=g++          # C++20; PCH once, then every tests/*.cpp -> .o
make CXX=clang++
make regrammar        # regenerate include/ctpy/python.hpp (needs Tablewright)
make single-header    # single-header/ctpy.hpp (needs python3 + quom)
cmake -B build && cmake --build build -j && ctest --test-dir build
```

The constexpr interpreter needs a raised evaluation budget; the Makefile and
the CMake interface carry the flags (opt out with `-DCTPY_CONSTEXPR_LIMITS=OFF`).

## Architecture

`source → prelex (constexpr: indentation → INDENT/DEDENT markers, strings
protected, comments stripped) → CTLL (q)LL(1) parse over the marker stream
(table generated by Tablewright from python.lark) → type-level AST →
constexpr tree-walk interpreter (value-level arena heap of ctc containers)
→ flatten the reachable result → right-size via ctc::shrunk into per-script
static storage → uniform ctpy::value views`. `include/ctll/` and
`include/ctc/` are vendored; `include/ctpy/python.hpp` is generated — edit
`python.lark` and `make regrammar`.

## License

Apache-2.0 with LLVM Exceptions (see LICENSE, NOTICE — CTLL is Hana
Dusíková's, from CTRE via notre; ctc is MIT; the parse table is generated by
Tablewright).
