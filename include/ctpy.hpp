#ifndef CTPY__HPP
#define CTPY__HPP

// ctpy: compile-time Python. Your script runs while your code compiles:
//
//   constexpr auto out = ctpy::run<R"py(
//   answer = 6 * 7
//   print("the answer is", answer)
//   )py">();
//
//   static_assert(out.ok());
//   static_assert(out.stdout() == "the answer is 42\n");
//   static_assert(out["answer"].to<int>() == 42);
//
// The script is pre-lexed (indentation becomes INDENT/DEDENT markers),
// parsed by a Tablewright-generated (q)LL(1) table driven by CTLL, and
// executed by a constexpr tree-walking interpreter whose heap is built
// on ctc containers. Results are right-sized into static storage:
// stdout, globals (as uniform null-object ctpy::value views), and any
// Python exception are all compile-time values.
//
// Under construction: see README.md for the v0.1 subset and the
// C++ <-> Python boundary design (arg<>, lift<>, file<>, pymodule<>).

#include "ctpy/version.hpp"
#include "ctpy/prelex.hpp"

#endif
