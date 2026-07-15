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

## Progress notes

- **M1 prelex: DONE.** Marker-stream golden tests in tests/prelex.cpp.
- **M2 grammar: DONE.** `include/ctpy/python.lark` (Lark dialect,
  `@action` hooks) generates conflict-free under `tablewright --check
  --lang=lark` (Q-grammar, 204 nonterminals / 579 productions before
  factoring, 78 actions); `make regrammar` emits python.hpp. Design
  notes vs. section 3, learned the hard way at character level:
  - *Expression tiers are NOT stratified in the grammar.* At character
    level `*` vs `**`, `<` vs `<<` vs `<=` and `=` vs `==` make
    textbook tiers non-LL(1) (the `pow_rest` epsilon conflicts with
    `term_rest` on `*`). Instead the grammar recognizes one flat
    operand/operator loop (`xloop`, statement-level `sloop`) with
    single-character dispatchers, and actions.hpp folds the pending
    stack with a Python precedence table (Pratt folding on the type
    stack; `**` right-assoc, chained comparisons fold into one
    compare node). AST-shape asserts pin the precedence.
  - *Keyword vs identifier* is ctlark-style chains with residual
    character classes (NCN* = name chars minus the expected letter) at
    statement starts (if/while/for/def/return/break/continue/pass) and
    operand starts (None/True/False/not, f-string prefix). At operator
    positions keywords need only a boundary guard (@kw_reject). One
    extra discipline everywhere a keyword just ended at a name
    boundary: the continuation may not start with a name character
    unless a space intervenes (`bval`/`opnd_sym`/`sloop_nb`), which is
    exactly Python's tokenization ("ifx"/"x ory" are names).
  - *elif/else attachment is flat*: clauses are statement-level items
    the actions attach to the still-attachable if/while/for node on
    top of the stack (rejecting stray/misplaced clauses), so suites
    never need context-split statement lists.
  - Reserved words as plain names are rejected semantically in
    @mk_name/@mk_attr against the full Python keyword list ("del x",
    "import os", "lambda = 1" all fail is_valid).
  - 78 actions (plan said ~35-45): the string machinery (three-state
    triple-quote counting x2 quote kinds) and the aug-assign/compare
    dispatchers cost more, but each action is a few lines.
  - Grammar subset exclusions beyond section 3 (all rejected, easy to
    add later): exponent literals (1e5), hex/oct/bin, leading-dot
    floats (.5), implicit string concatenation ('a' 'b'), r/b/u
    prefixes, semicolons, tuple subscripts a[1,2], parenthesized or
    subscript/attribute for-targets, bare testlist as for-iterable
    (use parens: `for x in (a, b):`), annotations. Known superset
    acceptances (invalid Python that parses; harmless before M9
    diagnostics): `1if x else 2`-style digit-keyword adjacency,
    `return`/`break` outside their proper context (semantic checks
    land with the interpreter).
- M2 wiring: text.hpp (`ctpy::text<Cs...>` + to_sv), ast.hpp
  (type-level nodes), actions.hpp (context<>/mk::* + precedence
  folding), parse.hpp (`detail::parse_def`, `detail::parsed_module`,
  public `ctpy::is_valid<Src>`), tests/parse.cpp (positive/negative +
  AST-shape static_asserts).
- **M3 expressions: DONE.** object.hpp (Kind/Object/Pair/Binding/Frame,
  `Arena<Objs,Chars,Pairs,Frames,Out>` of append-only ctc pools,
  `State<>` with globals/frame scopes + raise_error, `ex_kind` with
  Python-spelled enumerators so `exception() == ctpy::TypeError`
  reads right, truncating PyError message) and eval.hpp
  (`eval<Node>(State&)` -> object index; literals cook escapes and
  parse int/float spellings at evaluation; or/and return the deciding
  OPERAND; chained comparisons short-circuit per link; Python floor
  div/mod signs; `**` right-assoc with negative exponent -> float;
  interim `ctpy::eval<"expr">()` -> eval_result with
  ok()/exception()/to<T>()/str(), replaced by M8). v0.1 int bounds:
  `**`/`<<` overflow raises OverflowError (CPython would bignum), a
  non-integral float exponent raises TypeError (no constexpr pow in
  C++20); plain +/-/* overflow is still a constexpr hard error
  (unguarded). Bitwise/shift/in/is implemented beyond the milestone
  list since the AST already carries them; container branches of
  object_eq/contains are pre-wired for M6.

- **M4 statements/control: DONE.** exec.hpp (`exec<Stmt>(State&) ->
  Flow` value-level tree-walk: expr/assign/aug-assign/pass,
  if/elif/else, while and for with break/continue/else, Flow signals
  threaded through suites; chained assignment evaluates the value once
  then binds targets left-to-right; tuple targets unpack any
  tuple/list/str/range iterable with CPython's ValueError arity
  messages; module-level break/continue/return become soft
  SyntaxErrors) and builtins.hpp (name-call dispatch: bound names
  shadow builtins, else the builtin table - just `range` for now, a
  LAZY range object: Kind::range whose start/stop/step live as three
  pool ints, iterated arithmetically, `in`/`==` computed without
  materialization). Interim `ctpy::run<Src>()` -> run_result: executes
  the module body and snapshots globals (`ok()`, `exception()`,
  `["name"]` -> run_value with to<T>()/str()/exists()/kind); container
  globals report their Kind with an empty payload until the M8 views.
  tuple/list DISPLAY evaluation landed early in eval.hpp (unpacking and
  for-iteration need them): elements evaluate in order, then the
  element objects are copied into one contiguous pool run. Semantic
  checks live where CPython puts them: `range = 3; range(2)` is
  "'int' object is not callable", `for i in 5` is "not iterable".
  Tests tests/control.cpp incl. the sum-0..4 checkpoint.

- **M5 functions: DONE.** def/return/call in exec.hpp + the call
  dispatch in builtins.hpp; tests/functions.cpp incl. the recursive
  fib(10)==55 checkpoint. The section-4.5 type-erasure choice: a
  **table of thunks**, not a variadic module walk. Executing a def
  instantiates `fn_thunk<Def, St>::call` - a constexpr function owning
  everything type-level about the def (param names, body suite) - and
  registers its POINTER in `State::thunks` (deduped, one slot per def
  even when the def re-executes in a loop); the function Object stores
  that index in `i` and its def-time-evaluated defaults as the
  `[first, first+count)` object-pool run. Call sites dispatch through
  the pointer without naming the def's type, which also breaks
  template mutual recursion by construction (recursive calls cross a
  VALUE, not a type). Rejected walk: O(defs) per call and blind to
  functions created at run time (nested/conditional def, aliases).
  Return values ride `State::retval` next to Flow::return_ (read by
  the calling thunk before anything else can run). Arity errors spell
  CPython's TypeErrors exactly (takes/missing forms, name lists,
  was/were). The soft RecursionError guard raises "maximum recursion
  depth exceeded" at depth 100: ~7 constexpr frames per Python call
  keeps the worst case near ~700, safely under -fconstexpr-depth=1024
  (boundary pinned by tests: count(99) works, count(100) raises).
  Documented v0.1 deviations: NO closures (a nested def sees globals +
  its own locals only - enclosing locals are a NameError), and calls
  are positional-only (a kwarg at a call site is a soft TypeError;
  builtin sep/end kwargs land with print in M7). break/continue
  escaping a function body are the same soft SyntaxErrors as at module
  level.

## Risks

Constexpr budget blowups (mitigations: LL(1) not Earley, loops not recursion
in eval, append-only arena, one final right-size, canary); template depth on
big scripts (flat variadic Suites, `-fbracket-depth=2048`); grammar LL(1)
conflicts during authoring (`--check`/`--explain` in the loop; comprehensions
deferred); f-string mini-parser (scope-limited: `{expr}` only); gcc/clang
divergence (CI matrix, structured assertions); vendoring drift (sync scripts
in M10).
