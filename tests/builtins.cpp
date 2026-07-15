#include <ctpy.hpp>

// M7 checkpoint: the complete v0.1 builtin set. print (sep/end
// keywords, captured stdout), len, range, sum, min, max, abs, str,
// int, bool, sorted, enumerate, zip - every expectation below is the
// EXACT string/value CPython produces (verified offline), except where
// a comment flags a documented v0.1 divergence.

using ctpy::run;
using ctpy::eval;
using ctpy::Kind;

// --- print: str() to captured stdout ------------------------------------------

static_assert(run<"print()\n">().stdout() == "\n");
static_assert(run<"print(1, 2, 3)\n">().stdout() == "1 2 3\n");
static_assert(run<"print('hello')\n">().stdout() == "hello\n"); // str, no quotes
static_assert(run<"print('x', 42, 2.5)\n">().stdout() == "x 42 2.5\n");
static_assert(run<"print(True, False, None)\n">().stdout() == "True False None\n");
static_assert(run<"print(-7)\n">().stdout() == "-7\n");

// the umbrella-header example, end to end
static_assert(run<R"py(
answer = 6 * 7
print("the answer is", answer)
)py">().stdout() == "the answer is 42\n");

// sep and end keywords (print is the one v0.1 builtin taking keywords)
static_assert(run<"print('a', 'b', sep='')\n">().stdout() == "ab\n");
static_assert(run<"print(1, 2, sep=', ', end='!\\n')\n">().stdout() == "1, 2!\n");
static_assert(run<"print(1.5, True, None, sep='|')\n">().stdout() == "1.5|True|None\n");
static_assert(run<"print(1, end='')\n">().stdout() == "1");
static_assert(run<"print(1, sep=None, end=None)\n">().stdout() == "1\n"); // None = default
static_assert(run<R"py(
print(1)
print(2)
)py">().stdout() == "1\n2\n");
static_assert(run<"print(1, sep=5)\n">().exception() == ctpy::TypeError);
static_assert(run<"print(1, sep=5)\n">().exception().message()
	== "sep must be None or a string, not int");
static_assert(run<"print(1, end=0.5)\n">().exception().message()
	== "end must be None or a string, not float");
// documented v0.1 divergence: CPython print() also accepts file=/flush=
static_assert(run<"print(1, file=2)\n">().exception().message()
	== "print() got an unexpected keyword argument 'file'");

// print writes NOTHING once an exception is in flight mid-statement
static_assert(run<R"py(
print(1)
print(1 // 0)
)py">().stdout() == "1\n");
static_assert(run<R"py(
print(1)
print(1 // 0)
)py">().exception() == ctpy::ZeroDivisionError);

// a bound name shadows the builtin
static_assert(run<R"py(
print = 5
print(1)
)py">().exception() == ctpy::TypeError);

// --- str() of every kind, and print's container reprs ---------------------------

static_assert(eval<"str()">().str() == "");
static_assert(eval<"str(42)">().str() == "42");
static_assert(eval<"str(-7)">().str() == "-7");
static_assert(eval<"str(True)">().str() == "True");
static_assert(eval<"str(False)">().str() == "False");
static_assert(eval<"str(None)">().str() == "None");
static_assert(eval<"str('x')">().str() == "x"); // already a str: unquoted
static_assert(eval<"str([1, 2])">().str() == "[1, 2]");
static_assert(eval<"str((1, 'a'))">().str() == "(1, 'a')");
static_assert(eval<"str(range(5))">().str() == "range(0, 5)");
static_assert(eval<"str(range(2, 10, 3))">().str() == "range(2, 10, 3)");

// strings INSIDE containers print as reprs, CPython quote choice included
static_assert(run<"print([1, 'a', True, None])\n">().stdout() == "[1, 'a', True, None]\n");
static_assert(run<R"(print(["it's"]))" "\n">().stdout() == "[\"it's\"]\n");
static_assert(run<R"(print(['a\nb', 'a\tb']))" "\n">().stdout() == "['a\\nb', 'a\\tb']\n");

// nested containers, recursively
static_assert(run<"print([[1, 2], (3,), {'a': [4]}])\n">().stdout()
	== "[[1, 2], (3,), {'a': [4]}]\n");
static_assert(run<"print(())\n">().stdout() == "()\n");
static_assert(run<"print((1,))\n">().stdout() == "(1,)\n"); // one-tuple comma
static_assert(run<"print({})\n">().stdout() == "{}\n");
static_assert(run<"print({1, 2})\n">().stdout() == "{1, 2}\n");
static_assert(run<"print({'k': 1, 'x': [1, 2]})\n">().stdout() == "{'k': 1, 'x': [1, 2]}\n");
static_assert(run<"print({'a': {1, 2}})\n">().stdout() == "{'a': {1, 2}}\n");

// --- float formatting (CPython repr shape; 16 significant digits) ----------------

static_assert(eval<"str(2.5)">().str() == "2.5");
static_assert(eval<"str(0.5)">().str() == "0.5");
static_assert(eval<"str(-0.5)">().str() == "-0.5");
static_assert(eval<"str(3.14)">().str() == "3.14");
static_assert(eval<"str(1.0)">().str() == "1.0"); // integral floats keep .0
static_assert(eval<"str(100.0)">().str() == "100.0");
static_assert(eval<"str(-0.0)">().str() == "-0.0");
static_assert(eval<"str(7.25)">().str() == "7.25");
static_assert(eval<"str(123456789.5)">().str() == "123456789.5");
static_assert(eval<"str(1 / 3)">().str() == "0.3333333333333333"); // == CPython
static_assert(eval<"str(0.0001)">().str() == "0.0001");            // last fixed exp
static_assert(eval<"str(2.0 ** -13)">().str() == "0.0001220703125");
static_assert(eval<"str(10.0 ** 16)">().str() == "1e+16");  // scientific from 1e16 up
static_assert(eval<"str(10.0 ** 15)">().str() == "1000000000000000.0");
static_assert(eval<"str(2.0 ** 10)">().str() == "1024.0");
// documented v0.1 divergence: 16 significant digits, where CPython's
// shortest-round-trip repr needs 17 ("0.30000000000000004")
static_assert(eval<"str(0.1 + 0.2)">().str() == "0.3");

// --- len() (M6) plus the sequence builtins over every iterable kind ---------------

static_assert(eval<"len('hello')">().to<int>() == 5);
static_assert(eval<"len(range(10))">().to<int>() == 10);

// --- sum ----------------------------------------------------------------------------

static_assert(eval<"sum([1, 2, 3])">().to<int>() == 6);
static_assert(eval<"sum([])">().to<int>() == 0);
static_assert(eval<"sum(range(1, 11))">().to<int>() == 55);
static_assert(eval<"sum(range(10, 0, -2))">().to<int>() == 30); // negative step
static_assert(eval<"sum((1.5, 2.5))">().to<double>() == 4.0);
static_assert(eval<"sum((1.5, 2.5))">().kind == Kind::float_);
static_assert(eval<"sum([1, 2], 10)">().to<int>() == 13); // explicit start
static_assert(eval<"sum({1: 'a', 2: 'b'})">().to<int>() == 3); // dict sums its keys
static_assert(eval<"sum(5)">().exception() == ctpy::TypeError);
static_assert(eval<"sum(5)">().exception().message() == "'int' object is not iterable");
static_assert(eval<"sum('ab')">().exception().message()
	== "unsupported operand type(s) for +: 'int' and 'str'");
static_assert(eval<"sum([1], 'a')">().exception().message()
	== "sum() can't sum strings [use ''.join(seq) instead]");
static_assert(eval<"sum([1], start=1)">().exception() == ctpy::TypeError); // v0.1: no keywords

// --- min / max: iterable form and 2+-scalar form -------------------------------------

static_assert(eval<"min(3, 1, 2)">().to<int>() == 1);
static_assert(eval<"max(3, 1, 2)">().to<int>() == 3);
static_assert(eval<"min([5])">().to<int>() == 5);
static_assert(eval<"max([3, 1, 4, 1, 5])">().to<int>() == 5);
static_assert(eval<"min(range(4, 20, 3))">().to<int>() == 4);
static_assert(eval<"max(range(4, 20, 3))">().to<int>() == 19);
static_assert(eval<"max('abc')">().str() == "c"); // strings compare lexicographically
static_assert(eval<"min('b', 'a')">().str() == "a");
static_assert(eval<"max(1, 2.5)">().to<double>() == 2.5);
static_assert(eval<"max(1, 2.5)">().kind == Kind::float_); // the OBJECT wins, not a cast
static_assert(eval<"min()">().exception() == ctpy::TypeError);
static_assert(eval<"min()">().exception().message() == "min expected at least 1 argument, got 0");
static_assert(eval<"max([])">().exception() == ctpy::ValueError);
static_assert(eval<"max([])">().exception().message() == "max() iterable argument is empty");
static_assert(eval<"min(1)">().exception().message() == "'int' object is not iterable");
static_assert(eval<"min(1, 'a')">().exception() == ctpy::TypeError); // unordered kinds

// --- abs -------------------------------------------------------------------------------

static_assert(eval<"abs(-5)">().to<int>() == 5);
static_assert(eval<"abs(5)">().to<int>() == 5);
static_assert(eval<"abs(-2.5)">().to<double>() == 2.5);
static_assert(eval<"abs(2.5)">().to<double>() == 2.5);
static_assert(eval<"abs(True)">().to<int>() == 1);
static_assert(eval<"abs(True)">().kind == Kind::int_); // bool -> int, like CPython
static_assert(eval<"str(abs(-0.0))">().str() == "0.0"); // the sign BIT clears
static_assert(eval<"abs('x')">().exception() == ctpy::TypeError);
static_assert(eval<"abs('x')">().exception().message() == "bad operand type for abs(): 'str'");

// --- int -------------------------------------------------------------------------------

static_assert(eval<"int()">().to<int>() == 0);
static_assert(eval<"int(42)">().to<int>() == 42);
static_assert(eval<"int(True)">().to<int>() == 1);
static_assert(eval<"int(True)">().kind == Kind::int_);
static_assert(eval<"int(False)">().to<int>() == 0);
static_assert(eval<"int('42')">().to<int>() == 42);
static_assert(eval<"int('  -42 ')">().to<int>() == -42); // whitespace strips
static_assert(eval<"int('+7')">().to<int>() == 7);
static_assert(eval<"int(2.9)">().to<int>() == 2);   // truncation toward zero,
static_assert(eval<"int(-2.9)">().to<int>() == -2); // NOT floor
static_assert(eval<"int(9.0)">().to<int>() == 9);
static_assert(eval<"int('12a')">().exception() == ctpy::ValueError);
static_assert(eval<"int('12a')">().exception().message()
	== "invalid literal for int() with base 10: '12a'");
static_assert(eval<"int('')">().exception().message()
	== "invalid literal for int() with base 10: ''");
static_assert(eval<"int('- 1')">().exception() == ctpy::ValueError); // inner space
static_assert(eval<"int([1])">().exception() == ctpy::TypeError);
static_assert(eval<"int([1])">().exception().message()
	== "int() argument must be a string, a bytes-like object or a real number, not 'list'");
static_assert(eval<"int(None)">().exception().message()
	== "int() argument must be a string, a bytes-like object or a real number, not 'NoneType'");

// --- bool ------------------------------------------------------------------------------

static_assert(eval<"bool()">().to<bool>() == false);
static_assert(eval<"bool(0)">().to<bool>() == false);
static_assert(eval<"bool(3)">().to<bool>() == true);
static_assert(eval<"bool(-0.0)">().to<bool>() == false);
static_assert(eval<"bool('')">().to<bool>() == false);
static_assert(eval<"bool('x')">().to<bool>() == true);
static_assert(eval<"bool([])">().to<bool>() == false);
static_assert(eval<"bool([0])">().to<bool>() == true);
static_assert(eval<"bool(None)">().to<bool>() == false);
static_assert(eval<"bool(range(0))">().to<bool>() == false);
static_assert(eval<"bool(3)">().kind == Kind::boolean);
static_assert(eval<"bool(1, 2)">().exception() == ctpy::TypeError);
static_assert(eval<"bool(1, 2)">().exception().message()
	== "bool expected at most 1 argument, got 2");

// --- sorted: stable, always a fresh list ------------------------------------------------

static_assert(run<"print(sorted([3, 1, 2]))\n">().stdout() == "[1, 2, 3]\n");
static_assert(run<"print(sorted((3, 1, 2)))\n">().stdout() == "[1, 2, 3]\n");
static_assert(run<"print(sorted('cba'))\n">().stdout() == "['a', 'b', 'c']\n");
static_assert(run<"print(sorted(range(3, 0, -1)))\n">().stdout() == "[1, 2, 3]\n");
static_assert(run<"print(sorted({'b': 1, 'a': 2}))\n">().stdout() == "['a', 'b']\n"); // keys
static_assert(run<"print(sorted(['bb', 'aa', 'ab']))\n">().stdout()
	== "['aa', 'ab', 'bb']\n");
static_assert(run<"print(sorted([]))\n">().stdout() == "[]\n");
// stability: equal keys keep source order (True == 1, and 1 arrived first)
static_assert(run<"print(sorted([2, 1, True]))\n">().stdout() == "[1, True, 2]\n");
// sorting does NOT mutate the input
static_assert(run<R"py(
xs = [3, 1]
ys = sorted(xs)
print(xs, ys)
)py">().stdout() == "[3, 1] [1, 3]\n");
static_assert(run<"print(sorted([1, 'a']))\n">().exception() == ctpy::TypeError);
static_assert(run<"print(sorted([1, 'a']))\n">().exception().message()
	== "'<' not supported between instances of 'str' and 'int'");
static_assert(run<"sorted(5)\n">().exception().message() == "'int' object is not iterable");
static_assert(run<"sorted([2, 1], reverse=True)\n">().exception() == ctpy::TypeError); // v0.1

// --- enumerate: a MATERIALIZED list of (index, value) - documented divergence ------------

static_assert(run<"print(enumerate('ab'))\n">().stdout() == "[(0, 'a'), (1, 'b')]\n");
static_assert(run<"print(enumerate('ab', 1))\n">().stdout() == "[(1, 'a'), (2, 'b')]\n");
static_assert(run<"print(enumerate([], 5))\n">().stdout() == "[]\n");
static_assert(run<"print(len(enumerate('abc')))\n">().stdout() == "3\n");
static_assert(run<R"py(
for i, c in enumerate('abc'):
    print(i, c)
)py">().stdout() == "0 a\n1 b\n2 c\n");
static_assert(run<"enumerate(5)\n">().exception().message() == "'int' object is not iterable");
static_assert(run<"enumerate('ab', 'x')\n">().exception().message()
	== "'str' object cannot be interpreted as an integer");
static_assert(run<"enumerate('ab', 1, 2)\n">().exception().message()
	== "enumerate() takes at most 2 arguments (3 given)");

// --- zip: shortest input wins; also a MATERIALIZED list ------------------------------------

static_assert(run<"print(zip([1, 2, 3], 'ab'))\n">().stdout() == "[(1, 'a'), (2, 'b')]\n");
static_assert(run<"print(zip('ab'))\n">().stdout() == "[('a',), ('b',)]\n");
static_assert(run<"print(zip())\n">().stdout() == "[]\n");
static_assert(run<"print(zip('abc', range(10), [True]))\n">().stdout()
	== "[('a', 0, True)]\n");
static_assert(run<R"py(
total = 0
for a, b in zip((1, 2), (3, 4)):
    total += a * b
print(total)
)py">().stdout() == "11\n");
static_assert(run<"zip('a', 2)\n">().exception() == ctpy::TypeError);
static_assert(run<"zip('a', 2)\n">().exception().message() == "'int' object is not iterable");

// --- builtins compose --------------------------------------------------------------------

static_assert(eval<"sum(zip([1, 2], [3, 4])[0])">().to<int>() == 4);
static_assert(eval<"max(sorted([3, 1, 2]))">().to<int>() == 3);
static_assert(eval<"int(str(-321))">().to<int>() == -321);
static_assert(eval<"str(min([2.5, 1.5]))">().str() == "1.5");
static_assert(run<R"py(
def shout(xs):
    print(len(xs), max(xs), sep=': ')

shout([2, 7, 1])
)py">().stdout() == "3: 7\n");

// v0.1: builtins are not first-class values (documented)
static_assert(run<"f = len\n">().exception() == ctpy::NameError);

int main() { }
