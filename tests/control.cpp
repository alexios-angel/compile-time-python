#include <ctpy.hpp>

// M4 checkpoint: statements and control flow. Every assert parses real
// source (prelex -> (q)LL(1) -> AST) and EXECUTES it at compile time
// through exec<> / ctpy::run<>. A raising script is never a build
// failure: it reports ok()==false with the exception queryable.

using ctpy::run;
using ctpy::eval;
using ctpy::Kind;

// --- the PLAN.md checkpoint: sum 0..4 == 10 via for + range -------------

static_assert(run<
"total = 0\n"
"for i in range(5):\n"
"    total += i\n">().ok());
static_assert(run<
"total = 0\n"
"for i in range(5):\n"
"    total += i\n">()["total"].to<int>() == 10);

// --- assignment: plain, chained, tuple unpacking --------------------------

static_assert(run<"x = 1">()["x"].to<int>() == 1);
static_assert(run<"x = 1\ny = x + 2\n">()["y"].to<int>() == 3);
static_assert(run<"a = b = c = 7">()["a"].to<int>() == 7);
static_assert(run<"a = b = c = 7">()["b"].to<int>() == 7);
static_assert(run<"a = b = c = 7">()["c"].to<int>() == 7);
static_assert(run<"x = y = 'hi'">()["y"].str() == "hi");
static_assert(run<"a, b = 1, 2">()["a"].to<int>() == 1);
static_assert(run<"a, b = 1, 2">()["b"].to<int>() == 2);

// the swap: the right side is fully evaluated before any target binds
static_assert(run<"a, b = 1, 2\na, b = b, a\n">()["a"].to<int>() == 2);
static_assert(run<"a, b = 1, 2\na, b = b, a\n">()["b"].to<int>() == 1);

// unpacking any iterable: list, str, range - and nested targets
static_assert(run<"a, b, c = [10, 20, 30]">()["b"].to<int>() == 20);
static_assert(run<"a, b = 'xy'">()["a"].str() == "x");
static_assert(run<"a, b = 'xy'">()["b"].str() == "y");
static_assert(run<"a, b, c = range(3)">()["c"].to<int>() == 2);
static_assert(run<"a, (b, c) = 1, (2, 3)">()["c"].to<int>() == 3);
static_assert(run<"a, = 5,">()["a"].to<int>() == 5);

// arity mismatches are CPython's ValueErrors, non-iterables TypeError
static_assert(!run<"a, b = 1, 2, 3">().ok());
static_assert(run<"a, b = 1, 2, 3">().exception() == ctpy::ValueError);
static_assert(run<"a, b = 1, 2, 3">().exception().message()
	== "too many values to unpack (expected 2)");
static_assert(run<"a, b, c = 1, 2">().exception() == ctpy::ValueError);
static_assert(run<"a, b, c = 1, 2">().exception().message()
	== "not enough values to unpack (expected 3, got 2)");
static_assert(run<"a, b = 5">().exception() == ctpy::TypeError);
static_assert(run<"a, b = 5">().exception().message()
	== "cannot unpack non-iterable int object");

// --- aug-assign --------------------------------------------------------------

static_assert(run<"x = 10\nx += 5\n">()["x"].to<int>() == 15);
static_assert(run<"x = 10\nx -= 3\n">()["x"].to<int>() == 7);
static_assert(run<"x = 10\nx *= 3\n">()["x"].to<int>() == 30);
static_assert(run<"x = 10\nx //= 3\n">()["x"].to<int>() == 3);
static_assert(run<"x = 10\nx %= 3\n">()["x"].to<int>() == 1);
static_assert(run<"x = 2\nx **= 10\n">()["x"].to<int>() == 1024);
static_assert(run<"x = 1\nx <<= 4\n">()["x"].to<int>() == 16);
static_assert(run<"x = 16\nx >>= 2\n">()["x"].to<int>() == 4);
static_assert(run<"x = 6\nx &= 3\n">()["x"].to<int>() == 2);
static_assert(run<"x = 6\nx |= 1\n">()["x"].to<int>() == 7);
static_assert(run<"x = 6\nx ^= 3\n">()["x"].to<int>() == 5);
static_assert(run<"x = 7\nx /= 2\n">()["x"].kind == Kind::float_);
static_assert(run<"x = 7\nx /= 2\n">()["x"].to<double>() == 3.5);
static_assert(run<"s = 'a'\ns += 'b'\n">()["s"].str() == "ab");
static_assert(run<"x += 1">().exception() == ctpy::NameError);
static_assert(run<"x = 'a'\nx -= 1\n">().exception() == ctpy::TypeError);

// --- if / elif / else, nested -------------------------------------------------

static_assert(run<"x = 0\nif 1 < 2:\n    x = 1\n">()["x"].to<int>() == 1);
static_assert(run<"x = 0\nif 1 > 2:\n    x = 1\n">()["x"].to<int>() == 0);
static_assert(run<"if 1 > 2:\n    x = 1\nelse:\n    x = 2\n">()["x"].to<int>() == 2);
static_assert(run<
"n = 3\n"
"if n == 1:\n"
"    r = 'one'\n"
"elif n == 2:\n"
"    r = 'two'\n"
"elif n == 3:\n"
"    r = 'three'\n"
"else:\n"
"    r = 'many'\n">()["r"].str() == "three");
static_assert(run<
"n = 9\n"
"if n == 1:\n"
"    r = 'one'\n"
"elif n == 2:\n"
"    r = 'two'\n"
"else:\n"
"    r = 'many'\n">()["r"].str() == "many");

// nested ifs pick the right arm at every level
static_assert(run<
"a = 1\n"
"b = 2\n"
"r = 0\n"
"if a == 1:\n"
"    if b == 1:\n"
"        r = 11\n"
"    elif b == 2:\n"
"        r = 12\n"
"    else:\n"
"        r = 13\n"
"else:\n"
"    r = 20\n">()["r"].to<int>() == 12);

// inline suites and truthiness conditions
static_assert(run<"x = 0\nif 'nonempty': x = 1">()["x"].to<int>() == 1);
static_assert(run<"x = 0\nif '': x = 1">()["x"].to<int>() == 0);
static_assert(run<"x = 0\nif range(0):\n    x = 1\n">()["x"].to<int>() == 0);
static_assert(run<"x = 0\nif range(1):\n    x = 1\n">()["x"].to<int>() == 1);

// only the taken branch runs (the other side would raise)
static_assert(run<"if True:\n    x = 1\nelse:\n    x = 1 // 0\n">().ok());

// --- while: break, continue, else ------------------------------------------------

static_assert(run<
"i = 0\n"
"total = 0\n"
"while i < 5:\n"
"    i += 1\n"
"    total += i\n">()["total"].to<int>() == 15);

// continue skips the rest of the body, break leaves at once
static_assert(run<
"i = 0\n"
"total = 0\n"
"while True:\n"
"    i += 1\n"
"    if i > 10:\n"
"        break\n"
"    if i % 2 == 0:\n"
"        continue\n"
"    total += i\n">()["total"].to<int>() == 25);

// the else-suite runs on normal exhaustion, break skips it
static_assert(run<
"x = 0\n"
"while x < 3:\n"
"    x += 1\n"
"else:\n"
"    x = 99\n">()["x"].to<int>() == 99);
static_assert(run<
"x = 0\n"
"while True:\n"
"    x += 1\n"
"    if x == 3:\n"
"        break\n"
"else:\n"
"    x = 99\n">()["x"].to<int>() == 3);

// a raising condition surfaces softly
static_assert(!run<"while 1 // 0:\n    pass\n">().ok());
static_assert(run<"while 1 // 0:\n    pass\n">().exception() == ctpy::ZeroDivisionError);
static_assert(run<"while nope:\n    pass\n">().exception() == ctpy::NameError);

// --- for: range shapes, break/continue, else ----------------------------------------

static_assert(run<
"total = 0\n"
"for i in range(2, 11, 3):\n"
"    total += i\n">()["total"].to<int>() == 15); // 2 + 5 + 8
static_assert(run<
"total = 0\n"
"for i in range(5, 0, -1):\n"
"    total += i\n">()["total"].to<int>() == 15);
static_assert(run<
"x = 0\n"
"for i in range(0):\n"
"    x = 1\n"
"else:\n"
"    x = 2\n">()["x"].to<int>() == 2); // empty range: body never runs, else does

// classic search: for-else fires only without a break
static_assert(run<
"found = False\n"
"for i in range(10):\n"
"    if i == 7:\n"
"        found = True\n"
"        break\n"
"else:\n"
"    found = False\n">()["found"].to<bool>());
static_assert(!run<
"found = False\n"
"for i in range(5):\n"
"    if i == 7:\n"
"        found = True\n"
"        break\n"
"else:\n"
"    found = False\n">()["found"].to<bool>());

// continue in a for-loop
static_assert(run<
"total = 0\n"
"for i in range(10):\n"
"    if i % 2 == 0:\n"
"        continue\n"
"    total += i\n">()["total"].to<int>() == 25);

// an inner break leaves only the inner loop
static_assert(run<
"total = 0\n"
"for i in range(3):\n"
"    for j in range(3):\n"
"        if j == 1:\n"
"            break\n"
"        total += 1\n">()["total"].to<int>() == 3);

// the loop variable survives the loop (module scope, like CPython)
static_assert(run<"for i in range(4):\n    pass\n">()["i"].to<int>() == 3);

// --- for over str / list / tuple iterables ---------------------------------------------

static_assert(run<
"s = ''\n"
"for c in 'abc':\n"
"    s = c + s\n">()["s"].str() == "cba");
static_assert(run<
"total = 0\n"
"for v in [1, 2, 3]:\n"
"    total += v\n">()["total"].to<int>() == 6);
static_assert(run<
"total = 0\n"
"for v in (4, 5, 6):\n"
"    total += v\n">()["total"].to<int>() == 15);

// tuple targets unpack each element
static_assert(run<
"total = 0\n"
"for a, b in ((1, 2), (3, 4)):\n"
"    total += a * b\n">()["total"].to<int>() == 14);

// non-iterables raise CPython's TypeError
static_assert(run<"for i in 5:\n    pass\n">().exception() == ctpy::TypeError);
static_assert(run<"for i in 5:\n    pass\n">().exception().message()
	== "'int' object is not iterable");

// --- a raising loop body surfaces the exception with ok()==false ------------------------

static_assert(!run<
"x = 0\n"
"for i in range(3):\n"
"    x = 1 // 0\n">().ok());
static_assert(run<
"x = 0\n"
"for i in range(3):\n"
"    x = 1 // 0\n">().exception() == ctpy::ZeroDivisionError);
static_assert(run<
"x = 0\n"
"while x < 3:\n"
"    x += nope\n">().exception() == ctpy::NameError);

// the raise stops the loop dead: later iterations never run, and the
// globals keep the values from before the raise
static_assert(run<
"count = 0\n"
"for i in range(5):\n"
"    count += 1\n"
"    if count == 2:\n"
"        bad = 1 // 0\n">()["count"].to<int>() == 2);

// --- range() itself -----------------------------------------------------------------------

static_assert(eval<"5 in range(10)">().to<bool>());
static_assert(eval<"10 in range(10)">().ok());
static_assert(!eval<"10 in range(10)">().to<bool>());
static_assert(eval<"4 in range(0, 10, 2)">().to<bool>());
static_assert(!eval<"3 in range(0, 10, 2)">().to<bool>());
static_assert(eval<"3 in range(5, 0, -1)">().to<bool>());
static_assert(eval<"2.0 in range(3)">().to<bool>());
static_assert(!eval<"2.5 in range(3)">().to<bool>());
static_assert(eval<"range(3) == range(0, 3)">().to<bool>());
static_assert(eval<"range(0) == range(4, 4, 2)">().to<bool>());
static_assert(!eval<"range(3) == range(4)">().to<bool>());
static_assert(eval<"range(5)">().kind == Kind::range);
static_assert(run<"r = range(True)">()["r"].kind == Kind::range); // bool is int-like

static_assert(run<"x = range()">().exception() == ctpy::TypeError);
static_assert(run<"x = range()">().exception().message()
	== "range expected at least 1 argument, got 0");
static_assert(run<"x = range(1, 2, 3, 4)">().exception() == ctpy::TypeError);
static_assert(run<"x = range(1, 2, 0)">().exception() == ctpy::ValueError);
static_assert(run<"x = range(1, 2, 0)">().exception().message()
	== "range() arg 3 must not be zero");
static_assert(run<"x = range('a')">().exception() == ctpy::TypeError);
static_assert(run<"x = range('a')">().exception().message()
	== "'str' object cannot be interpreted as an integer");
static_assert(run<"x = range(1.5)">().exception() == ctpy::TypeError);

// a binding shadows the builtin; calling the shadow is not callable
static_assert(run<"range = 3\nx = range(2)\n">().exception() == ctpy::TypeError);
static_assert(run<"range = 3\nx = range(2)\n">().exception().message()
	== "'int' object is not callable");
static_assert(run<"nope(1)">().exception() == ctpy::NameError);

// --- statements at module level: pass, expression statements, empties ----------------------

static_assert(run<"">().ok());
static_assert(run<"pass">().ok());
static_assert(run<"pass\npass\n">().ok());
static_assert(run<"1 + 1">().ok());          // expression statement: value discarded
static_assert(run<"1 // 0">().exception() == ctpy::ZeroDivisionError);
static_assert(run<"# only a comment\n">().ok());

// break/continue that escape to module level are CPython's SyntaxErrors (soft)
static_assert(!run<"break">().ok());
static_assert(run<"break">().exception() == ctpy::SyntaxError);
static_assert(run<"break">().exception().message() == "'break' outside loop");
static_assert(run<"continue">().exception() == ctpy::SyntaxError);
static_assert(run<"if True:\n    break\n">().exception() == ctpy::SyntaxError);

// --- the interim result surface ---------------------------------------------------------------

static_assert(run<"x = 1">()["x"].exists());
static_assert(!run<"x = 1">()["never"].exists());
static_assert(run<"x = 1">()["never"].to<int>() == 0);       // null-object default
static_assert(run<"x = 1">()["never"].kind == Kind::none);
static_assert(run<"b = True">()["b"].kind == Kind::boolean);
static_assert(run<"b = True">()["b"].to<bool>());
static_assert(run<"f = 2.5">()["f"].to<double>() == 2.5);
static_assert(run<"n = None">()["n"].kind == Kind::none);
static_assert(run<"n = None">()["n"].exists());              // bound to None != unbound
static_assert(run<"s = 'ab' * 2">()["s"].str() == "abab");
static_assert(run<"t = 1, 2">()["t"].kind == Kind::tuple);   // payload lands with M8 views
static_assert(run<"xs = [1]">()["xs"].kind == Kind::list);

// an exception leaves the pre-raise globals readable
static_assert(run<"x = 41\ny = 1 // 0\n">()["x"].to<int>() == 41);
static_assert(!run<"x = 41\ny = 1 // 0\n">()["y"].exists());

// --- exec<Stmt>(State &): the raw statement entry point ------------------------------------------

namespace direct {

template <ctll::fixed_string Src> using module_of = ctpy::detail::parsed_module<Src>;

// drive a module against a live State, then read a global out of it
constexpr long long countdown_steps(long long start) {
	ctpy::State<> st{};
	st.bind("n", st.make_int(start));
	st.bind("steps", st.make_int(0));
	const ctpy::Flow flow = ctpy::exec<module_of<
		"while n > 0:\n"
		"    n -= 1\n"
		"    steps += 1\n">>(st);
	if (st.raised || flow != ctpy::Flow::next) {
		return -1;
	}
	return st.a.objs[st.lookup("steps")].i;
}
static_assert(countdown_steps(6) == 6);
static_assert(countdown_steps(0) == 0);

// a bare break comes back as a Flow signal when exec'd below module level
constexpr ctpy::Flow bare_break() {
	ctpy::State<> st{};
	return ctpy::exec<ctpy::ast::break_stmt>(st);
}
static_assert(bare_break() == ctpy::Flow::break_);

} // namespace direct

int main() { }
