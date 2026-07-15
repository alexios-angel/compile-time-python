# ctpy — compile-time Python

Header-only C++20 library (namespace `ctpy`): parses AND EXECUTES a Python
subset entirely at compile time. Repo:
github.com/alexios-angel/compile-time-python — work on `main`. C++20 ONLY
(deliberate family deviation: the interpreter leans on C++20 constexpr).
License: Apache-2.0 w/ LLVM Exceptions + NOTICE (CTLL via notre; ctc MIT;
Tablewright-generated table). The approved implementation plan lives at
the repo root in PLAN.md — read it before large changes.

Prefer ripgrep (`rg`) over `grep`.

## Build & test (compiling IS the test — suites are `static_assert`s)
```bash
make CXX=g++            # PCH of ctpy.hpp, then every tests/*.cpp -> .o
make CXX=clang++
make regrammar          # python.lark -> python.hpp via Tablewright (--lang=lark)
make parity-cases       # re-run tests/gen_expected.py under system python3
make single-header      # quom + LICENSE prepend
cmake -B build && cmake --build build -j && ctest --test-dir build
```
Flags fixed: `-std=c++20 -Iinclude -O2 -pedantic -Wall -Wextra -Werror
-Wconversion` + raised constexpr limits (gcc -fconstexpr-ops-limit=3e9
-fconstexpr-loop-limit=1e7 -fconstexpr-depth=1024; clang
-fconstexpr-steps=5e8 -fconstexpr-depth=1024 -fbracket-depth=2048).
CI: gcc 12–14, clang 15/16/18, C++20 only.

## Pipeline (script → result)
`prelex.hpp` (consteval: indentation → markers \x01 INDENT \x02 DEDENT
\x03 NEWLINE \x04 ENDMARKER; string-literal protection FIRST incl.
triple-quoted/f-strings; comments stripped; backslash + bracket
continuation; tabs → next multiple of 8, ambiguity = TabError; oversized
ctc::string → ctc::shrunk → ctll::fixed_string NTTP)
→ `python.hpp` (GENERATED (q)LL(1) table; source of truth `python.lark`)
→ `actions.hpp` (CTLL type-stack actions, ctlark context<>/text<Cs...>/mk::*
pattern) → `ast.hpp` (type-level nodes)
→ `eval.hpp` (`eval<Node>(State&)`/`exec<Stmt>(State&)` value-level
tree-walk; `object.hpp` Arena: tagged Object + index-range pools of ctc
vectors, append-only, transient so NOT structural; Flow signals for
break/continue/return; st.raised short-circuit for exceptions;
RecursionError guard at depth 100)
→ `result.hpp`/`views.hpp` (right-size via ctc::shrunk into per-Src static
storage; uniform ctpy::value null-object views, ctjson views.hpp analog)
→ `bind.hpp` (arg<>/file<>/stdin_text<>/pymodule<>/lift<>).
Diagnostics: `diag.hpp` (`error_info<src>()` stage/kind/line/column,
`error_message<src>()` caret render); tracebacks ride `ast::lined<N,Stmt>`
line stamps (prelex lines table -> @bump_line counter -> exec stamps
`State::current_line` -> `PyError::line`).

## Gotchas (load-bearing)
- **ctc and ctll are git SUBMODULES, never edit here:**
  `external/compile-time-containers` (ctc) and `external/compile-time-lark`
  (ctll) — run `git submodule update --init` once after cloning; bump by
  checking out a new commit inside the submodule and committing the gitlink.
  The build adds `<sub>/include` AND `<sub>/include/ctc` /
  `<sub>/include/ctll` to the include path so the headers' relative
  `"../ctc.hpp"`-style includes (incl. the GENERATED python.hpp, whose
  include Tablewright hardcodes) resolve via the quoted-include fallback;
  the CMake install flattens everything back to include/{ctpy,ctc,ctll}.
  `include/ctpy/python.hpp` is GENERATED — edit `python.lark`,
  `make regrammar`.
- **Grammar is (q)LL(1) at character level** (no lexer): keywords vs
  identifiers need maximal-munch left-factoring; assignment-vs-expression
  is factored through `testlist expr_stmt_tail` where Q-grammar
  shift-beats-epsilon does the disambiguation. Check with
  `tablewright --check` / `--explain NT` before committing grammar edits.
- A raising Python script is NOT a build failure (`ok()==false`,
  `exception()` queryable); a non-parsing script IS (static_assert), with
  `is_valid<>`/`error_message<>()` as the soft path.
- Python exception machinery is st.raised + early-outs — NEVER C++
  exceptions (none in constexpr).
- clangd parses these headers pre-C++20 and reports FALSE errors; trust
  the real g++/clang build.
- Tests include `<ctpy.hpp>` via -Iinclude; keep new suites in that form
  so the PCH and single-header stay honest.
