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
sorted, enumerate, zip; the str method set (ASCII semantics):
split/rsplit/splitlines, join, strip/lstrip/rstrip, upper/lower/casefold/
swapcase/capitalize/title, replace, find/rfind/index/rindex/count,
startswith/endswith, isdigit/isalpha/isalnum/isspace/isupper/islower,
zfill/ljust/rjust/center, removeprefix/removesuffix, partition/rpartition;
f-strings (`{expr}`, `{{`/`}}`, no format specs);
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

- **M6 containers: DONE.** Container semantics live in eval.hpp
  (displays, subscript_load/slice_load/subscript_store, list_append,
  hashable, set/dict equality), exec.hpp (subscript assignment + aug
  targets) and builtins.hpp (len, method-call dispatch); tests in
  tests/containers.cpp. Key decisions:
  - *Mutation = the realloc pattern* (section 4.3, arena append-only):
    list.append and a NEW dict key copy the element/pair run to the
    end of the pool plus the addition, then repoint the container
    Object IN PLACE (same pool slot), so every holder of that index
    sees the mutation; in-range `lst[i] = x` and existing-key
    `d[k] = v` overwrite their slot directly. Old runs stay behind as
    garbage until the M8 right-size. Documented v0.1 aliasing
    deviation: displays copy element OBJECTS into contiguous runs, so
    mutating a list AFTER storing it inside another container does not
    update the outer copy (dict VALUES are pair indices and do alias).
  - Indexing/slicing/len/for all share one iteration seam
    (iterable_kind/iter_len/iter_get, moved exec.hpp -> eval.hpp):
    str indexes/slices via the char pool, range stays lazy (indexing
    is arithmetic; SLICING a range mints a new lazy range), dict
    iterates keys in insertion order. Slices follow CPython's
    PySlice_AdjustIndices clamp rules exactly (None == absent bound;
    step 0 is ValueError). Negative indices count from the end
    everywhere.
  - Sets/dicts are insertion-ordered runs with linear-scan equality
    lookup (no hashing - fine at compile-time scale); displays dedupe
    keeping the FIRST key slot, a duplicate dict key updates its value
    in place (CPython order semantics). Unhashable keys/needles
    (list/set/dict, tuples containing them) raise TypeError.
  - Methods dispatch on Kind + name via
    `call_expr<attribute_expr<Obj, Name>, Args...>` in builtins.hpp:
    list.append, dict.keys/.values/.items/.get (views materialize as
    lists; items() lays element runs first, then the tuple headers as
    one contiguous run that IS the list run). Unknown attribute =
    AttributeError (added to ex_kind). Bare `d.keys` without the call
    stays a hard static_assert (no bound-method objects in v0.1).
  - Slice ASSIGNMENT (`xs[0:2] = ...`) is a soft "ctpy v0.1: slice
    assignment is not supported" TypeError; tuple/str item assignment
    is CPython's TypeError; KeyError spells the missing key repr-style
    via append_repr. detail::dec moved builtins.hpp -> object.hpp so
    eval.hpp error messages can spell counts.

- **M7 builtins + f-strings: DONE.** builtins.hpp completes the v0.1
  builtin table (print/len/range/sum/min/max/abs/str/int/bool/sorted/
  enumerate/zip) plus the shared Python str()/repr() formatter;
  fstring.hpp evaluates fstr_lit nodes; run_result gained interim
  stdout() (2048-char cap until the M8 right-size). Tests
  tests/builtins.cpp + tests/fstring.cpp, expectations verified against
  CPython 3.14. Design notes:
  - *One str() machinery for everything*: write_object(st, sink, index,
    repr) recurses through containers (elements always repr) and NEVER
    allocates - it only reads pools and pushes chars - so a str_sink
    target stays the contiguous tail of the char pool while it walks;
    print uses the same walker with a stdout_sink. repr of a str does
    CPython's quote choice + \n\r\t\xHH escapes.
  - *Float repr is 16 significant digits* (trailing zeros trimmed,
    CPython's fixed/scientific thresholds at 1e16/1e-4, ".0" on
    integral values, round-HALF-EVEN when trimming to 16 - naive +0.5
    double-rounds 0.1+0.2 visibly). Divergence documented: CPython's
    shortest-round-trip repr needs 17 digits for some values (0.1+0.2
    prints "0.3" here). Everything spellable in <=16 digits matches.
  - *Keywords exist only for print(sep=, end=)*: the call evaluator now
    splits args into positional + kw_pass arrays (evaluated in source
    order); def'd functions still reject keywords (M5 policy), every
    other builtin raises "X() takes no keyword arguments" (documented
    divergence: CPython's sum(start=)/sorted(key=,reverse=)).
  - *enumerate/zip MATERIALIZE lists of tuples* (documented; iteration,
    len(), indexing agree with CPython). Layout trick shared with
    sorted(): materialize_run() pushes contiguous element copies (str
    shares the char pool per-character, range synthesizes ints, dict
    yields keys), then rows, then the tuple headers as one contiguous
    run that IS the list run. sorted() is an in-place insertion sort
    over its materialized run (stable, compare_op(lt), never
    allocates); zip caps at 16 iterables (fixed scratch).
  - int(str) parses sign/whitespace itself; overflow anywhere
    (int(huge_str), int(1e300), abs(INT64_MIN)) is a soft ctpy-spelled
    OverflowError where CPython would bignum. Messages otherwise match
    CPython 3.14 exactly (incl. "min() iterable argument is empty",
    "print() got an unexpected keyword argument 'foo'").
  - *f-strings*: the M2 grammar kept bodies verbatim, so fstring.hpp
    does the plan's mini-parse: a consteval once-per-type scan splits
    literal segments from {expr} holes (bracket/quote-aware, so
    f'{d["k"]}' and f'{ {1:2}[1] }' work; blanks trim; {{ }} cook with
    the shared escape cooker), then each hole is re-run through the
    SAME Tablewright grammar via parsed_module over a re-packed
    ctll::fixed_string NTTP and must unwrap to module<expr_stmt<E>>.
    Evaluation is two-phase: all holes evaluate first (they allocate
    freely), THEN the result str is opened and parts append (nothing
    allocates), keeping its char run contiguous. Nested f-strings in
    the other quote kind work. Unsupported-in-v0.1 forms are HARD
    static_asserts with named messages (format specs {x:>8},
    conversions {x!r}, empty holes, lone/unterminated braces) - the
    module grammar already accepted the token, so family policy says
    the failure names the stage; is_valid<> stays parse-level only.
  - Builtins are still not first-class values (x = len is a NameError,
    documented in builtins.hpp).

- **M8 results/binding: DONE.** result.hpp (flatten + right-size +
  run/eval), views.hpp (ctpy::value), bind.hpp (arg/file/stdin_text/
  pymodule/lift/module); the interim M3/M4 result plumbing is gone;
  tests/results_binding.cpp mirrors every README snippet verbatim
  before the deeper coverage. Design notes vs. section 6:
  - *One result shape for everything*: run<Src>(seeds...),
    eval<Src>() and module<Src>.call<Fn>(args...) all return
    run_result<result_caps> - dense flattened pools (objs/chars/pairs
    as plain public ctc data), the bindings, right-sized stdout, the
    PyError channel, plus a result_obj slot that carries eval's
    expression value / call's return value (`result()`, with
    to<T>()/str()/.kind forwarded on the result itself, which is what
    keeps the M3-era `eval<...>().kind == Kind::range` asserts
    compiling untouched).
  - *The flattening pass* is a memoized garbage-collecting deep copy
    out of the dead State: runs are memoized by origin (first, count)
    so shared runs stay shared (and flat size stays <= arena size);
    container runs are RESERVED first (resize) then filled so they
    stay contiguous while element payloads land behind them; v0.1
    data cannot be cyclic (displays/append copy element objects), so
    recursion terminates at the data's nesting depth.
  - *Per-Src static constexpr results, exactly as planned, but only
    for SEED-FREE runs*: oversized_run<Src> (a variable template -
    the interpreter executes once per Src however many asserts read
    it) is right-sized pool-by-pool through ctc::shrunk into
    stored_run<Src>. A seeded run's sizes depend on the seed VALUES,
    which no function argument can lift to type level in C++20, so
    run<src>(args...) keeps the oversized capacities (documented in
    result.hpp; the user right-sizes lifted pieces via lift<> +
    shrunk). flat_global keeps const char* + length rather than a
    std::string_view because libstdc++'s string_view has private
    members (not structural) and would sink the shrunk NTTP.
  - *ctpy::value* is an eager view: scalars are copied in, str views
    its chars, containers carry pool pointers + their run, so a view
    is self-contained and operator[] derives child views on the fly
    (no per-type static arrays like ctjson needs - results here are
    values, not types). Python semantics in the views: dict
    subscripts are KEY lookups (str via [sv], int via [ll] - never
    positional), negative indices count from the end, sets do not
    subscript, str/range index/iterate by synthesizing 1-char str /
    int views, dicts iterate their keys; begin()/end() is a
    materializing iterator (elements have no common storage to point
    at). Misses are present==false null objects that chain forever.
  - *bind.hpp*: arg<> payloads are scalars, string literals /
    string_view (stored as a view of the caller's static storage),
    and ctc::vector/map/string nested arbitrarily (make_object
    overload recursion; a map's Compare order becomes dict insertion
    order). lift<> inverts it (vector from any iterable incl. lazy
    ranges, dict lifts keys like list(d); misses/mismatches lift
    EMPTY, family null-object policy). file<> mounts State::vfs;
    open() gained Kind::file + ex_kind has EOFError now; open is
    1-arg (int path = TypeError spelled like CPython's, documented
    deviation: real CPython would treat it as an fd), unmounted =
    OSError "[Errno 2] No such file or directory: 'path'", read()
    consumes (second read is ''). input() reads stdin_text<> lines
    (prompt -> stdout, EOFError "EOF when reading a line" when dry;
    an unterminated final line still reads). pymodule<> is the fixed
    v0.1 shape: validates its source parses, seeds nothing (import
    execution deferred). ctpy::module<Src> is a variable template
    (module_t instance) - `module` as an identifier is safe here,
    the contextual-keyword rules only bite line-initial directives -
    and .call<Fn> executes the body then dispatches the def's thunk,
    lifting arguments like arg<> payloads. All parity messages
    (open/input/EOFError/arity) re-verified against CPython 3.14.
  - run/eval/call are noexcept constexpr; a pool overflow inside is
    the ctc precondition trap (build error naming the violation),
    never an exception.

- **M9 diagnostics: DONE.** diag.hpp (error_stage/error_kind,
  ctpy::error_info<Src>() structured fields, error_message<Src>()
  caret rendering via the ctlark size-pass/fill-pass idiom, re-authored
  under ctpy::), traceback line numbers end to end, run<>'s stage-named
  hard error, and the CPython parity suite. Design notes:
  - *Line threading is a logical-line ORDINAL, not a physical line, at
    parse time.* The prelexer cannot tag the marker stream with numbers
    (the grammar would have to parse them), and CTLL actions never see
    the input position - so prelex emits a side table
    (prelex_result::lines, logical ordinal -> 1-based physical line,
    plus src_map: marker index -> raw source offset) and the PARSE
    counts logical lines itself: a type-level counter mk::lc<N> seeded
    at the bottom of the parse stack (parse.hpp start_context), bumped
    by the new @bump_line action placed after every consumed NEWL in
    the grammar (and only there). @end_stmt wraps each simple statement
    in ast::lined<N, Stmt>; compound headers stash the ordinal in their
    marker (ifm<N>/whilem<N>/form<N>/defm<N>/elifm<N>) and hdr_folded
    wraps when the suite closes; elif clauses carry their OWN line
    inside the clause_pack. exec of lined<N, S> stamps
    State::current_line through State::line_map (seeded per entry point
    from prelex_raw<Src>.lines); raise_error copies it into
    PyError::line. Loop executors re-stamp the header line before every
    test/target evaluation, so a while-test raising on iteration 40
    still reports the while line. Granularity is per-STATEMENT
    (documented deviation: expressions in a multi-line statement report
    the statement's first line - which is also CPython's line for the
    common cases, since a continued statement reports where it began).
  - *error_info is stage-first*: prelex failures carry the prelexer's
    own offset (fail() now records one: opening quote for unterminated
    strings, first content char for dedent errors); parse failures take
    CTLL's reject position IN THE MARKER STREAM and map it back through
    src_map before locate() computes line/column over the raw source.
    is_valid<> stays a plain bool; error_info/error_message never
    hard-error (valid sources report stage none / empty message).
  - *run/eval/module/pymodule hard-error naming the stage* via
    parse.hpp require_valid<Src>(): each stage's message sits on its
    own static_assert over a plain constexpr bool (the notre masking
    gotcha - a message on a compound dependent condition drowns in the
    expansion), and parse_stage_passed is vacuously true when prelex
    already failed, so exactly one message fires.
  - *ZeroDivisionError spellings unified to "division by zero"* (and
    int 0**-1 to "zero to a negative power"): CPython 3.14 - the
    repo's parity target - merged the old per-type variants.
  - *Parity harness*: tests/gen_expected.py runs 15 subset snippets
    under real python3 and writes tests/parity_cases.inc (checked in,
    CI stays hermetic); tests/parity.cpp static_asserts ok() +
    stdout()==CPython per case. Snippets avoid documented divergences
    (dict-view printing, 17-digit float reprs, set display order).

- **str methods: DONE.** builtins.hpp grows the v0.1 str method set
  (ASCII semantics), dispatched through the same Kind+name seam:
  split/rsplit (whitespace mode incl. sep=None, explicit-sep empty
  pieces, left/right-anchored maxsplit), splitlines (\n \r \r\n \v \f
  \x1c-\x1e, no trailing empty), join (two-phase like f-strings:
  materialize + type-check, then append - TypeError spells the item
  slot), strip/lstrip/rstrip (chars argument is a SET; None = ASCII
  whitespace), upper/lower/casefold(=lower)/swapcase/capitalize/title
  (title tracks prev-cased, so 'a1a' -> 'A1A'), replace (empty old
  inserts between chars and at both ends; count clamps from the left),
  find/rfind (-1 on miss), index/rindex (ValueError), count
  (non-overlapping; '' matches len+1), startswith/endswith,
  isdigit/isalpha/isalnum/isspace/isupper/islower (empty -> False;
  is-upper/lower need one cased char and all cased chars matching),
  zfill (sign-aware), ljust/rjust/center (CPython's odd-margin
  width-parity lean; fillchar validated before the width short-cut),
  removeprefix/removesuffix, partition/rpartition (3-tuples; empty sep
  is ValueError). Values and error spellings verified against CPython
  3.14; tests/strmethods.cpp pins them. Substring results SHARE the
  receiver's char run (str Objects are index ranges, immutable), and
  identity results return self. Documented v0.1 deviations (soft
  "ctpy v0.1" TypeErrors): no start/end slice forms on find/rfind/
  index/rindex/count/startswith/endswith, no tuple form for
  startswith/endswith, no keepends for splitlines, ASCII-only case/
  space/boundary tables (U+0085 etc. are not line boundaries).

## Risks

Constexpr budget blowups (mitigations: LL(1) not Earley, loops not recursion
in eval, append-only arena, one final right-size, canary); template depth on
big scripts (flat variadic Suites, `-fbracket-depth=2048`); grammar LL(1)
conflicts during authoring (`--check`/`--explain` in the loop; comprehensions
deferred); f-string mini-parser (scope-limited: `{expr}` only); gcc/clang
divergence (CI matrix, structured assertions); vendoring drift (sync scripts
in M10).
