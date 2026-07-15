#include <ctpy.hpp>

// M3 checkpoint: constexpr expression evaluation with Python
// semantics. Every assert parses real source (prelex -> (q)LL(1) ->
// AST) and then RUNS it through eval<> at compile time. A raising
// expression is never a build failure: it reports ok()==false with
// the exception queryable (the soft channel - st.raised + PyError,
// no C++ exceptions anywhere).

using ctpy::eval;
using ctpy::Kind;

// --- arithmetic, precedence, associativity ------------------------------

static_assert(eval<"2 + 3*4">().to<int>() == 14);
static_assert(eval<"(2 + 3)*4">().to<int>() == 20);
static_assert(eval<"2 ** 10">().to<int>() == 1024);
static_assert(eval<"2 ** 3 ** 2">().to<int>() == 512);   // ** is right-assoc
static_assert(eval<"-2 ** 2">().to<int>() == -4);        // unary minus folds UNDER **
static_assert(eval<"100 - 7 * 8">().to<int>() == 44);
static_assert(eval<"7 % 3">().to<int>() == 1);
static_assert(eval<"7 // 2">().to<int>() == 3);
static_assert(eval<"2 + 3*4">().kind == Kind::int_);

// Python floor division and modulo: floor toward -inf, remainder takes
// the divisor's sign (C++ truncates toward zero - the difference is
// the point of these asserts)
static_assert(eval<"-7 // 2">().to<int>() == -4);
static_assert(eval<"7 // -2">().to<int>() == -4);
static_assert(eval<"-7 // -2">().to<int>() == 3);
static_assert(eval<"-7 % 2">().to<int>() == 1);
static_assert(eval<"7 % -2">().to<int>() == -1);
static_assert(eval<"-7 % -2">().to<int>() == -1);

// true division is always float; negative exponents go float
static_assert(eval<"7 / 2">().kind == Kind::float_);
static_assert(eval<"7 / 2">().to<double>() == 3.5);
static_assert(eval<"8 / 2">().to<double>() == 4.0);
static_assert(eval<"2 ** -1">().kind == Kind::float_);
static_assert(eval<"2 ** -1">().to<double>() == 0.5);
static_assert(eval<"2 ** -2">().to<double>() == 0.25);
static_assert(eval<"10 ** 0">().to<int>() == 1);

// float literals (raw-spelling parse at evaluation) and float ops
static_assert(eval<"3.5">().to<double>() == 3.5);
static_assert(eval<"10.">().to<double>() == 10.0);
static_assert(eval<"1.5 + 1.5">().to<double>() == 3.0);
static_assert(eval<"2.5 * 2">().to<double>() == 5.0);
static_assert(eval<"2.5 * 2">().kind == Kind::float_);
static_assert(eval<"-7.0 // 2">().to<double>() == -4.0);
static_assert(eval<"-7.5 % 2">().to<double>() == 0.5);
static_assert(eval<"2.0 ** 3">().to<double>() == 8.0);
static_assert(eval<"1 + 2.0">().kind == Kind::float_);
static_assert(eval<"1 + 2.0">().to<double>() == 3.0);

// bitwise and shifts (bool acts as int, ~True == -2)
static_assert(eval<"5 & 3">().to<int>() == 1);
static_assert(eval<"5 | 3">().to<int>() == 7);
static_assert(eval<"5 ^ 3">().to<int>() == 6);
static_assert(eval<"1 << 10">().to<int>() == 1024);
static_assert(eval<"-8 >> 1">().to<int>() == -4);        // arithmetic shift floors
static_assert(eval<"~5">().to<int>() == -6);
static_assert(eval<"~True">().to<int>() == -2);
static_assert(eval<"True + True">().to<int>() == 2);     // bools are ints in arithmetic

// --- comparisons, incl. chaining ------------------------------------------

static_assert(eval<"1 < 2 < 3">().to<bool>());
static_assert(!eval<"1 < 2 < 2">().to<bool>());
static_assert(eval<"3 > 2 > 1">().to<bool>());
static_assert(eval<"1 <= 1 <= 1">().to<bool>());
static_assert(eval<"1 < 2 < 3">().kind == Kind::boolean);
static_assert(eval<"1 == 1.0">().to<bool>());            // numeric kinds compare by value
static_assert(eval<"True == 1">().to<bool>());
static_assert(!eval<"'1' == 1">().to<bool>());           // unrelated kinds: unequal, no error
static_assert(eval<"'1' == 1">().ok());
static_assert(eval<"1 != 2">().to<bool>());
static_assert(eval<"None is None">().to<bool>());
static_assert(!eval<"None is not None">().to<bool>());
static_assert(eval<"1 is not None">().to<bool>());

// a failing link short-circuits the rest of the chain (Python: the
// 1/0 is never evaluated because 1 > 2 already decided the result)
static_assert(eval<"1 > 2 < (1/0)">().ok());
static_assert(!eval<"1 > 2 < (1/0)">().to<bool>());

// --- or / and: short-circuit, and the OPERAND is the result -----------------

static_assert(eval<"True or (1/0)">().ok());             // rhs never evaluated
static_assert(eval<"True or (1/0)">().to<bool>());
static_assert(eval<"False and (1/0)">().ok());
static_assert(!eval<"False and (1/0)">().to<bool>());
static_assert(eval<"1 or undefined_name">().ok());       // no NameError either
static_assert(eval<"0 and undefined_name">().ok());
static_assert(eval<"0 or 5">().to<int>() == 5);          // returns the operand, not a bool
static_assert(eval<"0 or 5">().kind == Kind::int_);
static_assert(eval<"2 and 3">().to<int>() == 3);
static_assert(eval<"0 and 3">().to<int>() == 0);
static_assert(eval<"'' or 'x'">().str() == "x");
static_assert(eval<"'a' and 'b'">().str() == "b");
static_assert(eval<"not 1 == 2">().to<bool>());          // not binds looser than ==

// --- ternary (only the taken branch runs) -------------------------------------

static_assert(eval<"1 if 2 > 1 else 0">().to<int>() == 1);
static_assert(eval<"1 if 2 < 1 else 0">().to<int>() == 0);
static_assert(eval<"'a' if False else 'b'">().str() == "b");
static_assert(eval<"1 if True else 1/0">().ok());
static_assert(eval<"1/0 if False else 7">().to<int>() == 7);
static_assert(eval<"1 if 'x' else 2">().to<int>() == 1); // condition by truthiness

// --- strings ---------------------------------------------------------------------

static_assert(eval<"'abc'">().str() == "abc");
static_assert(eval<"'ab' + 'cd'">().str() == "abcd");
static_assert(eval<"'ab' * 3">().str() == "ababab");
static_assert(eval<"3 * 'ab'">().str() == "ababab");
static_assert(eval<"'ab' * 0">().str() == "");
static_assert(eval<"'ab' * -1">().str() == "");
static_assert(eval<"'a' in 'abc'">().to<bool>());
static_assert(eval<"'d' not in 'abc'">().to<bool>());
static_assert(eval<"'bc' in 'abc'">().to<bool>());
static_assert(eval<"'a' < 'b'">().to<bool>());
static_assert(eval<"'abc' == 'ab' + 'c'">().to<bool>());
static_assert(eval<"'a\\nb'">().str() == "a\nb");        // escapes cook at evaluation
static_assert(eval<"'it\\'s'">().str() == "it's");
static_assert(eval<"'a\\\\b'">().str() == "a\\b");
static_assert(eval<"'tab\\there'">().str() == "tab\there");

// --- truthiness -------------------------------------------------------------------

static_assert(eval<"not 0">().to<bool>());
static_assert(!eval<"not 3">().to<bool>());
static_assert(eval<"not 0.0">().to<bool>());
static_assert(eval<"not ''">().to<bool>());
static_assert(!eval<"not 'x'">().to<bool>());
static_assert(eval<"not None">().to<bool>());
static_assert(eval<"not False">().to<bool>());
static_assert(eval<"None">().ok());
static_assert(!eval<"None">().to<bool>());
static_assert(eval<"None">().kind == Kind::none);

// --- raising: the soft error channel (never a build failure) ------------------------

static_assert(!eval<"1/0">().ok());
static_assert(eval<"1/0">().exception() == ctpy::ZeroDivisionError);
static_assert(eval<"1/0">().exception().message() == "division by zero");
static_assert(eval<"1//0">().exception() == ctpy::ZeroDivisionError);
static_assert(eval<"1%0">().exception() == ctpy::ZeroDivisionError);
static_assert(eval<"1.0/0">().exception() == ctpy::ZeroDivisionError);
static_assert(eval<"0 ** -1">().exception() == ctpy::ZeroDivisionError);
static_assert(eval<"2 + 3/0 + 4">().exception() == ctpy::ZeroDivisionError);

static_assert(!eval<"undefined_name">().ok());
static_assert(eval<"undefined_name">().exception() == ctpy::NameError);
static_assert(eval<"y">().exception().message() == "name 'y' is not defined");
static_assert(eval<"x + 1">().exception() == ctpy::NameError);

static_assert(!eval<"1 + 'a'">().ok());
static_assert(eval<"1 + 'a'">().exception() == ctpy::TypeError);
static_assert(eval<"1 + 'a'">().exception().message()
	== "unsupported operand type(s) for +: 'int' and 'str'");
static_assert(eval<"'a' - 'b'">().exception() == ctpy::TypeError);
static_assert(eval<"'a' < 1">().exception() == ctpy::TypeError);
static_assert(eval<"-'a'">().exception() == ctpy::TypeError);
static_assert(eval<"1 in 2">().exception() == ctpy::TypeError);
static_assert(eval<"1 << -1">().exception() == ctpy::ValueError);

// the exception does not leak into unrelated evaluations (fresh State per eval)
static_assert(eval<"1 + 1">().ok());

// --- eval<Node>(State &): the raw interpreter entry point ----------------------------

namespace direct {

template <ctll::fixed_string Src>
using expr_of = typename ctpy::detail::single_expr<ctpy::detail::parsed_module<Src>>::type;

// seed a name into a live State, then evaluate an expression against it
constexpr long long with_x(long long x) {
	ctpy::State<> st{};
	st.bind("x", st.make_int(x));
	const std::uint32_t value = ctpy::eval<expr_of<"x * 2 + 2">>(st);
	return st.raised ? -1 : st.a.objs[value].i;
}
static_assert(with_x(20) == 42);
static_assert(with_x(-1) == 0);

// an unbound name through the raw entry point sets the soft channel
constexpr ctpy::ex_kind unbound() {
	ctpy::State<> st{};
	(void)ctpy::eval<expr_of<"nope">>(st);
	return st.error.type;
}
static_assert(unbound() == ctpy::NameError);

} // namespace direct

int main() { }
