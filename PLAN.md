# compile-time-python (ctpy) — Final Plan

## Context

New sibling in the compile-time-* family (github.com/alexios-angel): **parse
and EXECUTE Python scripts entirely at compile time**. C++20 only — which
makes it the first legitimate consumer of compile-time-containers (ctc), per
the earlier determination. Grammar authored in **Lark dialect**, lowered by
**Tablewright** (its Lark frontend) to a character-level (q)LL(1) CTLL table —
honoring "you can use lark, .g4, or EBNF". v0.1 runs a substantial Python
subset via a constexpr tree-walking interpreter; `std::embed` IO and the
Python stdlib get first-class seams now, implementations later.
License (user decided): **Apache-2.0 w/ LLVM Exceptions whole-repo** +
NOTICE crediting CTLL (Hana Dusíková/CTRE via notre), ctc (MIT), Tablewright.

## The look and feel (user-facing design)

Namespace `ctpy`, umbrella `<ctpy.hpp>`, all family conventions (tabs/K&R,
`CTPY__*__HPP` guards, `CTPY_EXPORT`, static_assert test suites, Makefile+PCH
with raised constexpr limits, CMake `ctpy::ctpy`, quom single-header, CI
gcc 12–14 / clang 15/16/18, C++20 only).

```cpp
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
static_assert(out["answer"].to<int>() == 55);    // globals readable
```

- **`ctpy::run<src>(args...)`** → result: `ok()`, `exception()`
  (type/message/line — a *raising* script does NOT fail the build),
  `stdout()` (string_view into static storage), `operator[]("global")` →
  `ctpy::value` (uniform, null-object chaining view: `out["a"]["b"][0]`),
  `.globals()` iteration.
- **Parse failure policy** (family style): `run` hard-errors naming the
  stage; `ctpy::is_valid<src>` / `ctpy::error_message<src>()` (caret
  diagnostic) never hard-error.
- **Sugar:** `ctpy::eval<"2 ** 10">().to<int>()`; `ctpy::module<src>` parses
  once, `.call<"fib">(20)` invokes a def.
- **C++ → Python:** `ctpy::arg<"name">(value)` seeds globals — scalars,
  string literals, and ctc containers lift in.
- **Python → C++:** `value.to<T>()`, `.str()`, indexing/chaining, and
  `ctpy::lift<ctc::vector<int,16>>(out["xs"])` into ctc containers
  (then `ctc::shrunk` as usual).
- **IO seam (std::embed-ready):** `open()` raises OSError unless a
  compile-time VFS is mounted: `ctpy::file<"config.txt", "contents">`;
  `input()` reads `ctpy::stdin_text<"...">`. When std::embed lands,
  `ctpy::file<"path">` gains a real-file overload — nothing else changes.
- **stdlib seam:** `import` resolves against a compile-time module registry;
  `ctpy::pymodule<"name", src>` mounts user modules (descriptor shape fixed
  in v0.1; `import` execution deferred). CPython's pure-Python stdlib
  sources become registry entries later.

### v0.1 Python subset

int (long long), float (basic), bool, None, str, list, tuple, dict, set;
variables, tuple assignment, aug-assign, chained assignment; full stratified
operators (or/and/not, chained comparisons, in/is, bitwise, shifts, +- */ //
% ** unary, ternary); call/index/attr postfix; slicing `a[i:j:k]`;
if/elif/else, while+break/continue+else, for over range/list/str/dict(+else);
def (positional + default args, recursion with RecursionError guard at depth
100, **no closures** — nested defs see globals + own locals only); return;
builtins: print (sep/end), len, range, sum, min, max, abs, str, int, bool,
sorted, enumerate, zip; f-strings (`{expr}`, `{{`/`}}`, no format specs);
comments. **Deferred:** classes, generators, comprehensions, try/except,
import execution, with, decorators, global/nonlocal, async.

## Architecture (decided)

1. **Parser: Tablewright (q)LL(1), NOT Earley.** `include/ctpy/python.lark`
   (Lark dialect, `@action` hooks) → `make regrammar` →
   `tablewright --ll --q --lang=lark --input=include/ctpy/python.lark
   --output=include/ctpy/ --generator=cpp_ctll_v2 --cfg:fname=python.hpp
   --cfg:namespace=ctpy --cfg:guard=CTPY__PYTHON__HPP
   --cfg:grammar_name=python_grammar` (+ `--check` in CI).
   Rationale: the constexpr budget must go to the *interpreter*; CTLL's
   fold-expression trampoline is linear and cheap, Earley is step-hungry.
   The subset is LL(1)-shapeable (pgen heritage). Vendors **ctll + ctc
   only** — not ctlark.
2. **Pre-lexer** (`prelex.hpp`, consteval, value-level): rewrites source
   before parsing — markers `\x01`=INDENT `\x02`=DEDENT `\x03`=NEWLINE
   `\x04`=ENDMARKER; string-literal protection first (incl. triple-quoted,
   prefixes, f-strings copied verbatim); comment stripping; backslash +
   implicit bracket continuation; tabs advance to multiple of 8 (ambiguity =
   TabError); blank lines emit nothing; EOF flushes DEDENTs. Handoff:
   oversized `ctc::basic_string` (bound `2*len+4`) → `ctc::shrunk` →
   `ctll::fixed_string` NTTP.
3. **Grammar shape:** suite = inline simple_stmt | `\x03 \x01 statement+
   \x02`; assignment-vs-expression left-factored through
   `testlist expr_stmt_tail` (Q-grammar shift-beats-epsilon makes the bare
   expression fall-through correct); expression tiers stratified manually
   (no precedence mechanism); tuple-vs-paren by trailing-comma flag in the
   action; slice by optional-flanked colons; keyword-vs-identifier maximal
   munch via ctlark-style left-factoring (the fiddly part — extra tests).
   ~35–45 `@action` hooks following ctlark's marker/accumulator/fold pattern.
4. **AST:** type-level node structs (`ast.hpp`) built by CTLL actions
   (`actions.hpp`), ctlark `context<>`/`text<Cs...>`/`mk::*` pattern;
   `ctll::reject` for malformed structure (e.g. invalid assignment target).
5. **Interpreter:** value-level tree-walk — `eval<Node>(State&) -> Value`,
   `exec<Stmt>(State&) -> Flow`. **State is transient (never an NTTP), so
   the object model uses a real tagged struct, not structural tricks**:
   `Object {Kind; long long i; double f; index-ranges into pools}`;
   `Arena<Objs,Chars,Pairs,Frames,Out>` of ctc vectors (append-only, no GC;
   capacities = template params with defaults, overridable per run).
   Objects reference objects by pool index (never pointers). Scopes:
   globals + one locals frame per call. Exceptions: `st.raised` flag +
   short-circuit checks (no C++ exceptions); break/continue/return via
   `Flow` signals. RecursionError guard (default 100) fires below
   `-fconstexpr-depth=1024`. print → `stdout_buf`.
6. **Results:** run in oversized Arena → right-size via ctc `shrunk` into a
   per-`Src` `static constexpr` Result → flatten reachable globals into
   `static constexpr` arrays of `ctpy::value` views (value-level analog of
   ctjson views.hpp; children point at sub-arrays; chaining + runtime
   persistence for free).
7. **Diagnostics:** adapt ctlark diag.hpp's generic size-pass/fill-pass
   caret renderer into `ctpy::` (re-authored, not vendored).

## Repo file plan

```
compile-time-python/
  LICENSE (Apache-2.0 w/ LLVM Exceptions)  NOTICE  README.md  CLAUDE.md
  Makefile (PCH, raised limits, regrammar, single-header)  CMakeLists.txt
  ctpy.cppm  packaging/pkgconfig.pc.in  .github/workflows/tests.yml
  include/ctpy.hpp                     # umbrella + public API
  include/ctll{,.hpp}                  # VENDORED (compile-time-lark)
  include/ctc{,.hpp}                   # VENDORED (compile-time-containers)
  include/ctpy/python.lark             # source of truth (Lark dialect)
  include/ctpy/python.hpp              # GENERATED (Tablewright) — never edit
  include/ctpy/{prelex,text,actions,ast,object,builtins,eval,fstring,
                result,views,bind,diag,debug}.hpp
  tests/{prelex,parse,expr,control,functions,containers,builtins,fstring,
         results_binding,diagnostics,parity}.cpp + CMakeLists.txt
  tests/gen_expected.py                # offline CPython-parity generator
  examples/{hello,fib,config}.cpp + Makefile/CMakeLists/README
  single-header/ctpy.hpp               # GENERATED (quom + LICENSE prepend)
```

Vendoring: extend compile-time-lark `tools/sync-vendor.sh` to sync
**ctll-only** into ctpy (script currently copies ctlark+ctll together —
needs a per-consumer component list); create a matching `tools/sync-vendor.sh`
in compile-time-containers pushing ctc into consumers. Until then, document
manual `cp -R` + `diff -rq` in CLAUDE.md.

## Milestones (each = static_assert checkpoint)

- **M1 prelex**: marker-stream golden tests (strings/comments/continuation/tabs).
- **M2 grammar**: `--check` conflict-free; `is_valid<"x = 1\n">`; AST-shape
  asserts (highest risk: keyword munch + assignment factoring — extra slack).
- **M3 expressions**: `eval<"2 + 3*4">().to<int>() == 14` etc.
- **M4 statements/control**: for/while/if; `sum 0..4 == 10`.
- **M5 functions**: fib(10)==55 recursion + defaults + RecursionError.
- **M6 containers**: list/tuple/dict/set ops, slicing, membership.
- **M7 builtins + f-strings**.
- **M8 results/binding**: `arg<>` in, `value`/`lift<>` out, VFS `file<>`.
- **M9 diagnostics**: caret messages; `run<"1/0\n">().exception()` ==
  ZeroDivisionError; is_valid never hard-errors.
- **M10 scaffolding**: CMake/CTest, CI, examples, single-header, cppm,
  sync-vendor wiring; `gh repo create alexios-angel/compile-time-python
  --public` + push (repo created at start; push per milestone or at end).

## Verification

- static_assert suites per milestone (compiling IS the test), both g++ 13
  and clang++ 22 locally under `-O2 -pedantic -Wall -Wextra -Werror
  -Wconversion` + raised constexpr limits; CI matrix after push.
- **CPython parity**: `tests/gen_expected.py` (offline, dev-only) runs
  snippets under real python3 → checked-in `parity_cases.inc` →
  `static_assert(run<SRC>().stdout() == EXPECTED)`. Hermetic CI.
- Budget canary test: a ~50-line script that must compile under the set
  limits (tripwire for constexpr-step regressions).
- Negative tests assert structured fields (kind/line/col), not compiler text.

## Risks

Constexpr budget blowups (mitigations: LL(1) not Earley, loops not recursion
in eval, append-only arena, one final right-size, canary); template depth on
big scripts (flat variadic Suites, `-fbracket-depth=2048`); grammar LL(1)
conflicts during authoring (`--check`/`--explain` in the loop; comprehensions
deferred); f-string mini-parser (scope-limited: `{expr}` only); gcc/clang
divergence (CI matrix, structured assertions); vendoring drift (sync scripts
in M10).
