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

static_assert(run<R"py(
def fib(n):
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)

answer = fib(10)
)py">().ok());
static_assert(run<R"py(
def fib(n):
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)

answer = fib(10)
)py">()["answer"].to<int>() == 55);

// --- factorial, iterative and recursive --------------------------------------

static_assert(run<R"py(
def fact(n):
    r = 1
    while n > 1:
        r *= n
        n -= 1
    return r

x = fact(10)
)py">()["x"].to<long long>() == 3628800);

static_assert(run<R"py(
def fact(n):
    if n <= 1:
        return 1
    return n * fact(n - 1)

x = fact(10)
)py">()["x"].to<long long>() == 3628800);

// return inside a loop unwinds the loop and the call, not the module
static_assert(run<R"py(
def find(limit):
    for i in range(limit):
        if i * i >= 10:
            return i
    return -1

a = find(10)
b = find(2)
)py">()["a"].to<int>() == 4);
static_assert(run<R"py(
def find(limit):
    for i in range(limit):
        if i * i >= 10:
            return i
    return -1

a = find(10)
b = find(2)
)py">()["b"].to<int>() == -1);

// --- parameters and scopes ------------------------------------------------------

// a parameter shadows a same-named global; locals never leak out
static_assert(run<R"py(
x = 1
def f(x):
    x = x + 1
    return x

y = f(5)
)py">()["y"].to<int>() == 6);
static_assert(run<R"py(
x = 1
def f(x):
    x = x + 1
    return x

y = f(5)
)py">()["x"].to<int>() == 1);
static_assert(!run<R"py(
def f(a):
    b = a
    return b

f(1)
c = b
)py">().ok()); // the local b is gone after the call

// globals are readable from inside a function
static_assert(run<R"py(
base = 10
def bump(n):
    return base + n

r = bump(5)
)py">()["r"].to<int>() == 15);

// assignment inside a function is LOCAL (no global statement in v0.1)
static_assert(run<R"py(
count = 0
def touch():
    count = 99
    return count

r = touch()
)py">()["count"].to<int>() == 0);

// --- defaults: evaluated ONCE, at def time -----------------------------------------

static_assert(run<R"py(
def add(a, b=10):
    return a + b

x = add(1)
y = add(1, 2)
)py">()["x"].to<int>() == 11);
static_assert(run<R"py(
def add(a, b=10):
    return a + b

x = add(1)
y = add(1, 2)
)py">()["y"].to<int>() == 3);

// the default captures the value the expression had AT DEF TIME
static_assert(run<R"py(
k = 5
def f(x=k):
    return x

k = 99
a = f()
b = f(1)
)py">()["a"].to<int>() == 5);
static_assert(run<R"py(
k = 5
def f(x=k):
    return x

k = 99
a = f()
b = f(1)
)py">()["b"].to<int>() == 1);

// re-executing a def re-evaluates its defaults (each def is a statement)
static_assert(run<R"py(
r = 0
for i in range(3):
    def f(x=i):
        return x

r = f()
)py">()["r"].to<int>() == 2);

// several defaults fill the parameter tail left to right
static_assert(run<R"py(
def g(a, b=2, c=3):
    return a * 100 + b * 10 + c

x = g(1)
y = g(1, 9)
z = g(1, 9, 8)
)py">()["x"].to<int>() == 123);
static_assert(run<R"py(
def g(a, b=2, c=3):
    return a * 100 + b * 10 + c

x = g(1)
y = g(1, 9)
z = g(1, 9, 8)
)py">()["y"].to<int>() == 193);
static_assert(run<R"py(
def g(a, b=2, c=3):
    return a * 100 + b * 10 + c

x = g(1)
y = g(1, 9)
z = g(1, 9, 8)
)py">()["z"].to<int>() == 198);

// a raising default expression surfaces at DEF time, softly
static_assert(run<R"py(
def f(x=1 // 0):
    return x
)py">().exception() == ctpy::ZeroDivisionError);

// --- functions calling functions, mutual recursion ---------------------------------

static_assert(run<R"py(
def double(n):
    return n * 2

def quad(n):
    return double(double(n))

r = quad(5)
)py">()["r"].to<int>() == 20);

// the callee resolves at CALL time, so a later def is visible
static_assert(run<R"py(
def is_even(n):
    if n == 0:
        return True
    return is_odd(n - 1)

def is_odd(n):
    if n == 0:
        return False
    return is_even(n - 1)

a = is_even(10)
b = is_odd(10)
)py">()["a"].to<bool>());
static_assert(!run<R"py(
def is_even(n):
    if n == 0:
        return True
    return is_odd(n - 1)

def is_odd(n):
    if n == 0:
        return False
    return is_even(n - 1)

a = is_even(10)
b = is_odd(10)
)py">()["b"].to<bool>());

// functions are first-class objects: aliases call the same function
static_assert(run<R"py(
def double(n):
    return n * 2

alias = double
r = alias(21)
)py">()["r"].to<int>() == 42);
static_assert(run<R"py(
def f():
    return 1
)py">()["f"].kind == Kind::function);

// a def can shadow a builtin, like any other binding
static_assert(run<R"py(
def range(n):
    return n + 1

r = range(4)
)py">()["r"].to<int>() == 5);

// --- nested def: globals + own locals only (NO closures, documented) -----------------

static_assert(run<R"py(
def outer(x):
    def inner(y):
        return y * 2
    return inner(x) + 1

r = outer(4)
)py">()["r"].to<int>() == 9);

// the inner function cannot see the enclosing call's locals (v0.1 has
// no closures; real Python would close over z and return 5)
static_assert(run<R"py(
def outer():
    z = 5
    def inner():
        return z
    return inner()

outer()
)py">().exception() == ctpy::NameError);
static_assert(run<R"py(
def outer():
    z = 5
    def inner():
        return z
    return inner()

outer()
)py">().exception().message() == "name 'z' is not defined");

// ...but it does see globals through the enclosing call
static_assert(run<R"py(
g = 7
def outer():
    def inner():
        return g
    return inner()

r = outer()
)py">()["r"].to<int>() == 7);

// the inner name dies with the enclosing call
static_assert(run<R"py(
def outer():
    def inner():
        return 1
    return inner()

r = outer()
inner()
)py">().exception() == ctpy::NameError);

// --- return without a value, falling off the end -> None ----------------------------

static_assert(run<R"py(
def f():
    return

x = f()
)py">()["x"].kind == Kind::none);
static_assert(run<R"py(
def f():
    return

x = f()
)py">()["x"].exists()); // bound to None, not unbound
static_assert(run<R"py(
def f():
    pass

x = f()
)py">()["x"].kind == Kind::none);
static_assert(run<R"py(
def f(n):
    if n > 0:
        return n

x = f(-1)
)py">()["x"].kind == Kind::none);

// --- the soft RecursionError guard ---------------------------------------------------

// infinite recursion is a SOFT RecursionError - the guard (depth 100)
// fires long before -fconstexpr-depth could hard-fail the build
static_assert(!run<R"py(
def f():
    return f()

f()
)py">().ok());
static_assert(run<R"py(
def f():
    return f()

f()
)py">().exception() == ctpy::RecursionError);
static_assert(run<R"py(
def f():
    return f()

f()
)py">().exception().message() == "maximum recursion depth exceeded");

// depth 100 is the boundary: 100 live calls work, the 101st raises
static_assert(run<R"py(
def count(n):
    if n == 0:
        return 0
    return count(n - 1) + 1

r = count(99)
)py">()["r"].to<int>() == 99);
static_assert(run<R"py(
def count(n):
    if n == 0:
        return 0
    return count(n - 1) + 1

r = count(100)
)py">().exception() == ctpy::RecursionError);

// --- wrong arity -> CPython's TypeErrors ----------------------------------------------

static_assert(run<R"py(
def f(a, b):
    return a

f(1)
)py">().exception() == ctpy::TypeError);
static_assert(run<R"py(
def f(a, b):
    return a

f(1)
)py">().exception().message()
	== "f() missing 1 required positional argument: 'b'");
static_assert(run<R"py(
def f(a, b):
    return a

f()
)py">().exception().message()
	== "f() missing 2 required positional arguments: 'a' and 'b'");
static_assert(run<R"py(
def f(a, b, c):
    return a

f()
)py">().exception().message()
	== "f() missing 3 required positional arguments: 'a', 'b', and 'c'");
static_assert(run<R"py(
def f(a, b):
    return a

f(1, 2, 3)
)py">().exception().message()
	== "f() takes 2 positional arguments but 3 were given");
static_assert(run<R"py(
def f():
    return 1

f(1)
)py">().exception().message()
	== "f() takes 0 positional arguments but 1 was given");
static_assert(run<R"py(
def f(a):
    return a

f(1, 2)
)py">().exception().message()
	== "f() takes 1 positional argument but 2 were given");
static_assert(run<R"py(
def g(a, b=1):
    return a

g(1, 2, 3)
)py">().exception().message()
	== "g() takes from 1 to 2 positional arguments but 3 were given");

// keyword arguments are out of the v0.1 call subset (soft TypeError)
static_assert(run<R"py(
def f(a):
    return a

f(a=1)
)py">().exception() == ctpy::TypeError);

// --- stray control flow inside a function body (grammar superset, soft) ---------------

static_assert(run<R"py(
def f():
    break

f()
)py">().exception() == ctpy::SyntaxError);
static_assert(run<R"py(
def f():
    break

f()
)py">().exception().message() == "'break' outside loop");
static_assert(run<R"py(
def f():
    continue

f()
)py">().exception() == ctpy::SyntaxError);

// an exception inside a call unwinds every live frame, softly
static_assert(run<R"py(
def inner():
    return 1 // 0

def outer():
    return inner()

outer()
)py">().exception() == ctpy::ZeroDivisionError);

// --- the thunk table stays one-slot-per-def --------------------------------------------

namespace direct {

template <ctll::fixed_string Src> using module_of = ctpy::detail::parsed_module<Src>;

// a def re-executed in a loop reuses its thunk slot (the table is
// per-def, only the function OBJECTS and their defaults are per-execution)
constexpr std::size_t thunk_slots() {
	ctpy::State<> st{};
	(void)ctpy::exec<module_of<R"py(
for i in range(5):
    def f(x=i):
        return x
)py">>(st);
	return st.raised ? std::size_t(0) : st.thunks.size();
}
static_assert(thunk_slots() == 1);

// two distinct defs get two slots
constexpr std::size_t two_defs() {
	ctpy::State<> st{};
	(void)ctpy::exec<module_of<R"py(
def f():
    return 1
def g():
    return 2
)py">>(st);
	return st.raised ? std::size_t(0) : st.thunks.size();
}
static_assert(two_defs() == 2);

// the locals frame pool truncates back after every call
constexpr bool frames_balanced() {
	ctpy::State<> st{};
	const std::size_t before = st.a.frames.size();
	(void)ctpy::exec<module_of<R"py(
def f(a, b):
    c = a + b
    return c
f(1, 2)
)py">>(st);
	// only the globals grew (the def's own name binding)
	return !st.raised && st.stack.empty() && st.a.frames.size() == before + 1;
}
static_assert(frames_balanced());

} // namespace direct

int main() { }
