#include <ctpy.hpp>

// M5 checkpoint: def / return / call. Function objects live in the
// arena and reference their def through a type-erased THUNK TABLE
// (State::thunks); defaults are evaluated once at def time; calls push
// one locals frame; recursion is guarded by the soft RecursionError at
// depth 100 (firing far below -fconstexpr-depth). v0.1 has NO closures:
// a nested def sees globals + its own locals only. A raising script is
// never a build failure - ok()==false with the exception queryable.

using ctpy::run;
using ctpy::Kind;

// --- the PLAN.md checkpoint: fib(10) == 55, recursively ---------------------

static_assert(run<
"def fib(n):\n"
"    if n < 2:\n"
"        return n\n"
"    return fib(n - 1) + fib(n - 2)\n"
"\n"
"answer = fib(10)\n">().ok());
static_assert(run<
"def fib(n):\n"
"    if n < 2:\n"
"        return n\n"
"    return fib(n - 1) + fib(n - 2)\n"
"\n"
"answer = fib(10)\n">()["answer"].to<int>() == 55);

// --- factorial, iterative and recursive --------------------------------------

static_assert(run<
"def fact(n):\n"
"    r = 1\n"
"    while n > 1:\n"
"        r *= n\n"
"        n -= 1\n"
"    return r\n"
"\n"
"x = fact(10)\n">()["x"].to<long long>() == 3628800);

static_assert(run<
"def fact(n):\n"
"    if n <= 1:\n"
"        return 1\n"
"    return n * fact(n - 1)\n"
"\n"
"x = fact(10)\n">()["x"].to<long long>() == 3628800);

// return inside a loop unwinds the loop and the call, not the module
static_assert(run<
"def find(limit):\n"
"    for i in range(limit):\n"
"        if i * i >= 10:\n"
"            return i\n"
"    return -1\n"
"\n"
"a = find(10)\n"
"b = find(2)\n">()["a"].to<int>() == 4);
static_assert(run<
"def find(limit):\n"
"    for i in range(limit):\n"
"        if i * i >= 10:\n"
"            return i\n"
"    return -1\n"
"\n"
"a = find(10)\n"
"b = find(2)\n">()["b"].to<int>() == -1);

// --- parameters and scopes ------------------------------------------------------

// a parameter shadows a same-named global; locals never leak out
static_assert(run<
"x = 1\n"
"def f(x):\n"
"    x = x + 1\n"
"    return x\n"
"\n"
"y = f(5)\n">()["y"].to<int>() == 6);
static_assert(run<
"x = 1\n"
"def f(x):\n"
"    x = x + 1\n"
"    return x\n"
"\n"
"y = f(5)\n">()["x"].to<int>() == 1);
static_assert(!run<
"def f(a):\n"
"    b = a\n"
"    return b\n"
"\n"
"f(1)\n"
"c = b\n">().ok()); // the local b is gone after the call

// globals are readable from inside a function
static_assert(run<
"base = 10\n"
"def bump(n):\n"
"    return base + n\n"
"\n"
"r = bump(5)\n">()["r"].to<int>() == 15);

// assignment inside a function is LOCAL (no global statement in v0.1)
static_assert(run<
"count = 0\n"
"def touch():\n"
"    count = 99\n"
"    return count\n"
"\n"
"r = touch()\n">()["count"].to<int>() == 0);

// --- defaults: evaluated ONCE, at def time -----------------------------------------

static_assert(run<
"def add(a, b=10):\n"
"    return a + b\n"
"\n"
"x = add(1)\n"
"y = add(1, 2)\n">()["x"].to<int>() == 11);
static_assert(run<
"def add(a, b=10):\n"
"    return a + b\n"
"\n"
"x = add(1)\n"
"y = add(1, 2)\n">()["y"].to<int>() == 3);

// the default captures the value the expression had AT DEF TIME
static_assert(run<
"k = 5\n"
"def f(x=k):\n"
"    return x\n"
"\n"
"k = 99\n"
"a = f()\n"
"b = f(1)\n">()["a"].to<int>() == 5);
static_assert(run<
"k = 5\n"
"def f(x=k):\n"
"    return x\n"
"\n"
"k = 99\n"
"a = f()\n"
"b = f(1)\n">()["b"].to<int>() == 1);

// re-executing a def re-evaluates its defaults (each def is a statement)
static_assert(run<
"r = 0\n"
"for i in range(3):\n"
"    def f(x=i):\n"
"        return x\n"
"\n"
"r = f()\n">()["r"].to<int>() == 2);

// several defaults fill the parameter tail left to right
static_assert(run<
"def g(a, b=2, c=3):\n"
"    return a * 100 + b * 10 + c\n"
"\n"
"x = g(1)\n"
"y = g(1, 9)\n"
"z = g(1, 9, 8)\n">()["x"].to<int>() == 123);
static_assert(run<
"def g(a, b=2, c=3):\n"
"    return a * 100 + b * 10 + c\n"
"\n"
"x = g(1)\n"
"y = g(1, 9)\n"
"z = g(1, 9, 8)\n">()["y"].to<int>() == 193);
static_assert(run<
"def g(a, b=2, c=3):\n"
"    return a * 100 + b * 10 + c\n"
"\n"
"x = g(1)\n"
"y = g(1, 9)\n"
"z = g(1, 9, 8)\n">()["z"].to<int>() == 198);

// a raising default expression surfaces at DEF time, softly
static_assert(run<"def f(x=1 // 0):\n    return x\n">().exception() == ctpy::ZeroDivisionError);

// --- functions calling functions, mutual recursion ---------------------------------

static_assert(run<
"def double(n):\n"
"    return n * 2\n"
"\n"
"def quad(n):\n"
"    return double(double(n))\n"
"\n"
"r = quad(5)\n">()["r"].to<int>() == 20);

// the callee resolves at CALL time, so a later def is visible
static_assert(run<
"def is_even(n):\n"
"    if n == 0:\n"
"        return True\n"
"    return is_odd(n - 1)\n"
"\n"
"def is_odd(n):\n"
"    if n == 0:\n"
"        return False\n"
"    return is_even(n - 1)\n"
"\n"
"a = is_even(10)\n"
"b = is_odd(10)\n">()["a"].to<bool>());
static_assert(!run<
"def is_even(n):\n"
"    if n == 0:\n"
"        return True\n"
"    return is_odd(n - 1)\n"
"\n"
"def is_odd(n):\n"
"    if n == 0:\n"
"        return False\n"
"    return is_even(n - 1)\n"
"\n"
"a = is_even(10)\n"
"b = is_odd(10)\n">()["b"].to<bool>());

// functions are first-class objects: aliases call the same function
static_assert(run<
"def double(n):\n"
"    return n * 2\n"
"\n"
"alias = double\n"
"r = alias(21)\n">()["r"].to<int>() == 42);
static_assert(run<"def f():\n    return 1\n">()["f"].kind == Kind::function);

// a def can shadow a builtin, like any other binding
static_assert(run<
"def range(n):\n"
"    return n + 1\n"
"\n"
"r = range(4)\n">()["r"].to<int>() == 5);

// --- nested def: globals + own locals only (NO closures, documented) -----------------

static_assert(run<
"def outer(x):\n"
"    def inner(y):\n"
"        return y * 2\n"
"    return inner(x) + 1\n"
"\n"
"r = outer(4)\n">()["r"].to<int>() == 9);

// the inner function cannot see the enclosing call's locals (v0.1 has
// no closures; real Python would close over z and return 5)
static_assert(run<
"def outer():\n"
"    z = 5\n"
"    def inner():\n"
"        return z\n"
"    return inner()\n"
"\n"
"outer()\n">().exception() == ctpy::NameError);
static_assert(run<
"def outer():\n"
"    z = 5\n"
"    def inner():\n"
"        return z\n"
"    return inner()\n"
"\n"
"outer()\n">().exception().message() == "name 'z' is not defined");

// ...but it does see globals through the enclosing call
static_assert(run<
"g = 7\n"
"def outer():\n"
"    def inner():\n"
"        return g\n"
"    return inner()\n"
"\n"
"r = outer()\n">()["r"].to<int>() == 7);

// the inner name dies with the enclosing call
static_assert(run<
"def outer():\n"
"    def inner():\n"
"        return 1\n"
"    return inner()\n"
"\n"
"r = outer()\n"
"inner()\n">().exception() == ctpy::NameError);

// --- return without a value, falling off the end -> None ----------------------------

static_assert(run<
"def f():\n"
"    return\n"
"\n"
"x = f()\n">()["x"].kind == Kind::none);
static_assert(run<
"def f():\n"
"    return\n"
"\n"
"x = f()\n">()["x"].exists()); // bound to None, not unbound
static_assert(run<
"def f():\n"
"    pass\n"
"\n"
"x = f()\n">()["x"].kind == Kind::none);
static_assert(run<
"def f(n):\n"
"    if n > 0:\n"
"        return n\n"
"\n"
"x = f(-1)\n">()["x"].kind == Kind::none);

// --- the soft RecursionError guard ---------------------------------------------------

// infinite recursion is a SOFT RecursionError - the guard (depth 100)
// fires long before -fconstexpr-depth could hard-fail the build
static_assert(!run<
"def f():\n"
"    return f()\n"
"\n"
"f()\n">().ok());
static_assert(run<
"def f():\n"
"    return f()\n"
"\n"
"f()\n">().exception() == ctpy::RecursionError);
static_assert(run<
"def f():\n"
"    return f()\n"
"\n"
"f()\n">().exception().message() == "maximum recursion depth exceeded");

// depth 100 is the boundary: 100 live calls work, the 101st raises
static_assert(run<
"def count(n):\n"
"    if n == 0:\n"
"        return 0\n"
"    return count(n - 1) + 1\n"
"\n"
"r = count(99)\n">()["r"].to<int>() == 99);
static_assert(run<
"def count(n):\n"
"    if n == 0:\n"
"        return 0\n"
"    return count(n - 1) + 1\n"
"\n"
"r = count(100)\n">().exception() == ctpy::RecursionError);

// --- wrong arity -> CPython's TypeErrors ----------------------------------------------

static_assert(run<
"def f(a, b):\n"
"    return a\n"
"\n"
"f(1)\n">().exception() == ctpy::TypeError);
static_assert(run<
"def f(a, b):\n"
"    return a\n"
"\n"
"f(1)\n">().exception().message()
	== "f() missing 1 required positional argument: 'b'");
static_assert(run<
"def f(a, b):\n"
"    return a\n"
"\n"
"f()\n">().exception().message()
	== "f() missing 2 required positional arguments: 'a' and 'b'");
static_assert(run<
"def f(a, b, c):\n"
"    return a\n"
"\n"
"f()\n">().exception().message()
	== "f() missing 3 required positional arguments: 'a', 'b', and 'c'");
static_assert(run<
"def f(a, b):\n"
"    return a\n"
"\n"
"f(1, 2, 3)\n">().exception().message()
	== "f() takes 2 positional arguments but 3 were given");
static_assert(run<
"def f():\n"
"    return 1\n"
"\n"
"f(1)\n">().exception().message()
	== "f() takes 0 positional arguments but 1 was given");
static_assert(run<
"def f(a):\n"
"    return a\n"
"\n"
"f(1, 2)\n">().exception().message()
	== "f() takes 1 positional argument but 2 were given");
static_assert(run<
"def g(a, b=1):\n"
"    return a\n"
"\n"
"g(1, 2, 3)\n">().exception().message()
	== "g() takes from 1 to 2 positional arguments but 3 were given");

// keyword arguments are out of the v0.1 call subset (soft TypeError)
static_assert(run<
"def f(a):\n"
"    return a\n"
"\n"
"f(a=1)\n">().exception() == ctpy::TypeError);

// --- stray control flow inside a function body (grammar superset, soft) ---------------

static_assert(run<
"def f():\n"
"    break\n"
"\n"
"f()\n">().exception() == ctpy::SyntaxError);
static_assert(run<
"def f():\n"
"    break\n"
"\n"
"f()\n">().exception().message() == "'break' outside loop");
static_assert(run<
"def f():\n"
"    continue\n"
"\n"
"f()\n">().exception() == ctpy::SyntaxError);

// an exception inside a call unwinds every live frame, softly
static_assert(run<
"def inner():\n"
"    return 1 // 0\n"
"\n"
"def outer():\n"
"    return inner()\n"
"\n"
"outer()\n">().exception() == ctpy::ZeroDivisionError);

// --- the thunk table stays one-slot-per-def --------------------------------------------

namespace direct {

template <ctll::fixed_string Src> using module_of = ctpy::detail::parsed_module<Src>;

// a def re-executed in a loop reuses its thunk slot (the table is
// per-def, only the function OBJECTS and their defaults are per-execution)
constexpr std::size_t thunk_slots() {
	ctpy::State<> st{};
	(void)ctpy::exec<module_of<
		"for i in range(5):\n"
		"    def f(x=i):\n"
		"        return x\n">>(st);
	return st.raised ? std::size_t(0) : st.thunks.size();
}
static_assert(thunk_slots() == 1);

// two distinct defs get two slots
constexpr std::size_t two_defs() {
	ctpy::State<> st{};
	(void)ctpy::exec<module_of<
		"def f():\n"
		"    return 1\n"
		"def g():\n"
		"    return 2\n">>(st);
	return st.raised ? std::size_t(0) : st.thunks.size();
}
static_assert(two_defs() == 2);

// the locals frame pool truncates back after every call
constexpr bool frames_balanced() {
	ctpy::State<> st{};
	const std::size_t before = st.a.frames.size();
	(void)ctpy::exec<module_of<
		"def f(a, b):\n"
		"    c = a + b\n"
		"    return c\n"
		"f(1, 2)\n">>(st);
	// only the globals grew (the def's own name binding)
	return !st.raised && st.stack.empty() && st.a.frames.size() == before + 1;
}
static_assert(frames_balanced());

} // namespace direct

int main() { }
