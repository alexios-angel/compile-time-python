#include <ctpy.hpp>

// The v0.1 str method set (ASCII semantics): split/rsplit/splitlines,
// join, strip/lstrip/rstrip, the case methods, replace, find/rfind/
// index/rindex/count, startswith/endswith, the is* predicates, zfill,
// ljust/rjust/center, removeprefix/removesuffix, partition/rpartition.
// Every expectation below is the EXACT value/message CPython 3.14
// produces (verified offline), except where a comment flags a
// documented v0.1 divergence (no start/end slice forms, no tuple
// startswith, no keepends, ASCII-only case/space tables).

using ctpy::run;
using ctpy::eval;
using ctpy::Kind;

// --- split: whitespace mode (no empty pieces, ends ignored) --------------------

static_assert(run<"print('a b  c'.split())\n">().stdout() == "['a', 'b', 'c']\n");
static_assert(run<"print(' a  b '.split())\n">().stdout() == "['a', 'b']\n");
static_assert(run<"print(''.split())\n">().stdout() == "[]\n");
static_assert(run<"print('   '.split())\n">().stdout() == "[]\n");
static_assert(run<R"(print(' a\tb\nc '.split()))" "\n">().stdout() == "['a', 'b', 'c']\n");
static_assert(run<"print('one'.split())\n">().stdout() == "['one']\n");

// sep=None is the same whitespace mode; maxsplit keeps the REST intact
// (trailing whitespace included, leading skipped)
static_assert(run<"print(' a b  c '.split(None, 1))\n">().stdout() == "['a', 'b  c ']\n");
static_assert(run<"print(' a b  c '.split(None, 0))\n">().stdout() == "['a b  c ']\n");
static_assert(run<"print('a b'.split(None))\n">().stdout() == "['a', 'b']\n");

// --- split: explicit sep (empty pieces preserved) --------------------------------

static_assert(run<"print('a,,b'.split(','))\n">().stdout() == "['a', '', 'b']\n");
static_assert(run<"print(''.split(','))\n">().stdout() == "['']\n");
static_assert(run<"print(',a,'.split(','))\n">().stdout() == "['', 'a', '']\n");
static_assert(run<"print('aXXbXXc'.split('XX'))\n">().stdout() == "['a', 'b', 'c']\n");
static_assert(run<"print('aaa'.split('aa'))\n">().stdout() == "['', 'a']\n"); // non-overlapping
static_assert(run<"print('abc'.split(','))\n">().stdout() == "['abc']\n");

// maxsplit: 0 splits nothing, negative means unlimited
static_assert(run<"print('a,b,c'.split(',', 1))\n">().stdout() == "['a', 'b,c']\n");
static_assert(run<"print('a,b,c'.split(',', 0))\n">().stdout() == "['a,b,c']\n");
static_assert(run<"print('a,b,c'.split(',', -1))\n">().stdout() == "['a', 'b', 'c']\n");
static_assert(run<"print('a,b,c'.split(',', 99))\n">().stdout() == "['a', 'b', 'c']\n");
static_assert(run<"print('one two three'.split(' ', 1))\n">().stdout()
	== "['one', 'two three']\n");

// the result is a real list of str
static_assert(eval<"'a b'.split()">().kind == Kind::list);
static_assert(eval<"'a,b'.split(',')">().result()[1].str() == "b");
static_assert(eval<"len('a,,b'.split(','))">().to<int>() == 3);
static_assert(eval<"'a,,b'.split(',')[1]">().str() == "");

// split misuse
static_assert(eval<"'ab'.split('')">().exception() == ctpy::ValueError);
static_assert(eval<"'ab'.split('')">().exception().message() == "empty separator");
static_assert(eval<"'ab'.split(5)">().exception() == ctpy::TypeError);
static_assert(eval<"'ab'.split(5)">().exception().message() == "must be str or None, not int");
static_assert(eval<"'ab'.split(',', 'x')">().exception() == ctpy::TypeError);
static_assert(eval<"'ab'.split(',', 'x')">().exception().message()
	== "'str' object cannot be interpreted as an integer");
static_assert(eval<"'ab'.split(',', None)">().exception().message()
	== "'NoneType' object cannot be interpreted as an integer");
static_assert(eval<"'ab'.split(',', 1, 2)">().exception() == ctpy::TypeError);
static_assert(eval<"'ab'.split(',', 1, 2)">().exception().message()
	== "split() takes at most 2 arguments (3 given)");

// --- rsplit: right-anchored maxsplit ----------------------------------------------

static_assert(run<"print('a,b,c'.rsplit(',', 1))\n">().stdout() == "['a,b', 'c']\n");
static_assert(run<"print(' a b  c '.rsplit(None, 1))\n">().stdout() == "[' a b', 'c']\n");
static_assert(run<"print(' a b  c '.rsplit(None, 0))\n">().stdout() == "[' a b  c']\n");
static_assert(run<"print(',a,'.rsplit(','))\n">().stdout() == "['', 'a', '']\n");
static_assert(run<"print('aaa'.rsplit('aa'))\n">().stdout() == "['a', '']\n"); // right-anchored
static_assert(run<"print(''.rsplit())\n">().stdout() == "[]\n");
static_assert(run<"print(''.rsplit(','))\n">().stdout() == "['']\n");
// without a limit, rsplit agrees with split
static_assert(run<"print('a,b,c'.rsplit(','))\n">().stdout() == "['a', 'b', 'c']\n");
static_assert(run<"print(' a b '.rsplit())\n">().stdout() == "['a', 'b']\n");
static_assert(eval<"'ab'.rsplit('')">().exception().message() == "empty separator");
static_assert(eval<"'ab'.rsplit(',', 1, 2)">().exception().message()
	== "rsplit() takes at most 2 arguments (3 given)");

// --- splitlines: \n \r \r\n (+ ASCII \v \f \x1c-\x1e), no trailing empty ------------

static_assert(run<R"(print('a\nb\n'.splitlines()))" "\n">().stdout() == "['a', 'b']\n");
static_assert(run<R"(print('a\n\nb'.splitlines()))" "\n">().stdout() == "['a', '', 'b']\n");
static_assert(run<"print(''.splitlines())\n">().stdout() == "[]\n");
static_assert(run<R"(print('\n'.splitlines()))" "\n">().stdout() == "['']\n");
static_assert(run<R"(print('a\rb\r\nc'.splitlines()))" "\n">().stdout() == "['a', 'b', 'c']\n");
static_assert(run<R"(print('a\vb\fc'.splitlines()))" "\n">().stdout() == "['a', 'b', 'c']\n");
static_assert(run<"print('no breaks'.splitlines())\n">().stdout() == "['no breaks']\n");
// documented v0.1 divergence: CPython's keepends form is not supported
static_assert(eval<"'a'.splitlines(True)">().exception() == ctpy::TypeError);

// --- join ---------------------------------------------------------------------------

static_assert(eval<"','.join(['a', 'b'])">().str() == "a,b");
static_assert(eval<"''.join(['a', 'b'])">().str() == "ab");
static_assert(eval<"', '.join(['a'])">().str() == "a");
static_assert(eval<"','.join([])">().str() == "");
static_assert(eval<"'-'.join('abc')">().str() == "a-b-c"); // any iterable of str
static_assert(eval<"','.join(('x', 'y'))">().str() == "x,y");
static_assert(eval<"','.join({'k': 1, 'z': 2})">().str() == "k,z"); // dict joins its keys
static_assert(eval<"'ab'.join(['', ''])">().str() == "ab");

// non-str elements are CPython's TypeError, spelled with the item slot
static_assert(eval<"','.join([1])">().exception() == ctpy::TypeError);
static_assert(eval<"','.join([1])">().exception().message()
	== "sequence item 0: expected str instance, int found");
static_assert(eval<"','.join(['a', 2])">().exception().message()
	== "sequence item 1: expected str instance, int found");
static_assert(eval<"','.join([True])">().exception().message()
	== "sequence item 0: expected str instance, bool found");
static_assert(eval<"','.join(range(2))">().exception().message()
	== "sequence item 0: expected str instance, int found");
static_assert(eval<"','.join(5)">().exception() == ctpy::TypeError);
static_assert(eval<"','.join(5)">().exception().message() == "can only join an iterable");
static_assert(eval<"','.join()">().exception().message()
	== "str.join() takes exactly one argument (0 given)");

// --- strip / lstrip / rstrip ----------------------------------------------------------

static_assert(eval<R"('  ab \t\n '.strip())">().str() == "ab");
static_assert(eval<"'  ab '.lstrip()">().str() == "ab ");
static_assert(eval<"'  ab '.rstrip()">().str() == "  ab");
static_assert(eval<"''.strip()">().str() == "");
static_assert(eval<"'aaa'.strip('a')">().str() == ""); // strips to empty
static_assert(eval<"'ab'.strip()">().str() == "ab");

// the chars argument is a SET of characters, not a prefix/suffix
static_assert(eval<"'xxabxx'.strip('x')">().str() == "ab");
static_assert(eval<"'xyxabyx'.strip('xy')">().str() == "ab");
static_assert(eval<"'xyxabyx'.lstrip('xy')">().str() == "abyx");
static_assert(eval<"'xyxabyx'.rstrip('xy')">().str() == "xyxab");
static_assert(eval<"'ab'.strip('')">().str() == "ab");   // empty set strips nothing
static_assert(eval<"'ab'.strip(None)">().str() == "ab"); // None means the default

static_assert(eval<"'ab'.strip(5)">().exception() == ctpy::TypeError);
static_assert(eval<"'ab'.strip(5)">().exception().message() == "strip arg must be None or str");
static_assert(eval<"'ab'.lstrip(5)">().exception().message() == "lstrip arg must be None or str");
static_assert(eval<"'ab'.rstrip(5)">().exception().message() == "rstrip arg must be None or str");
static_assert(eval<"'ab'.strip('x', 'y')">().exception().message()
	== "strip expected at most 1 argument, got 2");

// --- the case methods (ASCII) ----------------------------------------------------------

static_assert(eval<"'hEl_lo3'.upper()">().str() == "HEL_LO3");
static_assert(eval<"'HeL_LO3'.lower()">().str() == "hel_lo3");
static_assert(eval<"'AbC'.casefold()">().str() == "abc"); // == lower over ASCII
static_assert(eval<"'aBc Def'.swapcase()">().str() == "AbC dEF");
static_assert(eval<"'hELLO wORLD'.capitalize()">().str() == "Hello world");
static_assert(eval<"'3abc'.capitalize()">().str() == "3abc"); // digit first: rest lowers
static_assert(eval<"'abc def'.title()">().str() == "Abc Def");
static_assert(eval<"'ABC DEF'.title()">().str() == "Abc Def");
static_assert(eval<"'3b 4c'.title()">().str() == "3B 4C");   // digits do not continue words
static_assert(eval<"'a1a'.title()">().str() == "A1A");
static_assert(eval<R"("they're bill's".title())">().str() == "They'Re Bill'S");
static_assert(eval<"''.upper()">().str() == "");
static_assert(eval<"''.title()">().str() == "");
static_assert(eval<"''.capitalize()">().str() == "");
static_assert(eval<"'x'.upper(1)">().exception() == ctpy::TypeError);
static_assert(eval<"'x'.upper(1)">().exception().message()
	== "str.upper() takes no arguments (1 given)");
static_assert(eval<"'x'.title(1)">().exception().message()
	== "str.title() takes no arguments (1 given)");

// --- replace ------------------------------------------------------------------------------

static_assert(eval<"'aaa'.replace('a', 'b')">().str() == "bbb");
static_assert(eval<"'aaa'.replace('a', 'b', 2)">().str() == "bba");
static_assert(eval<"'aaa'.replace('a', 'b', 0)">().str() == "aaa");
static_assert(eval<"'aaa'.replace('a', 'b', -1)">().str() == "bbb"); // negative = all
static_assert(eval<"'banana'.replace('ana', 'X')">().str() == "bXna"); // non-overlapping
static_assert(eval<"'abc'.replace('x', 'y')">().str() == "abc");
static_assert(eval<"'abc'.replace('b', '')">().str() == "ac");
static_assert(eval<"'ab'.replace('a', 'xy')">().str() == "xyb"); // grows

// empty old inserts between characters and at both ends, like CPython
static_assert(eval<"'abc'.replace('', '-')">().str() == "-a-b-c-");
static_assert(eval<"'abc'.replace('', '-', 2)">().str() == "-a-bc");
static_assert(eval<"''.replace('', 'x')">().str() == "x");

static_assert(eval<"'ab'.replace('a')">().exception() == ctpy::TypeError);
static_assert(eval<"'ab'.replace('a')">().exception().message()
	== "replace() takes at least 2 positional arguments (1 given)");
static_assert(eval<"'ab'.replace(1, 'x')">().exception().message()
	== "replace() argument 1 must be str, not int");
static_assert(eval<"'ab'.replace('a', 2)">().exception().message()
	== "replace() argument 2 must be str, not int");
static_assert(eval<"'ab'.replace('a', 'b', 'c')">().exception().message()
	== "'str' object cannot be interpreted as an integer");
static_assert(eval<"'ab'.replace('a', 'b', 1, 2)">().exception().message()
	== "replace() takes at most 3 arguments (4 given)");

// --- find / rfind / index / rindex / count ---------------------------------------------------

static_assert(eval<"'hello'.find('l')">().to<int>() == 2);
static_assert(eval<"'hello'.rfind('l')">().to<int>() == 3);
static_assert(eval<"'hello'.find('x')">().to<int>() == -1);  // find misses to -1 ...
static_assert(eval<"'hello'.rfind('x')">().to<int>() == -1);
static_assert(eval<"'hello'.index('ll')">().to<int>() == 2);
static_assert(eval<"'banana'.rfind('an')">().to<int>() == 3);
static_assert(eval<"'banana'.rindex('na')">().to<int>() == 4);
static_assert(eval<"'hello'.find('')">().to<int>() == 0);
static_assert(eval<"''.find('')">().to<int>() == 0);
static_assert(eval<"'hello'.rfind('')">().to<int>() == 5);
static_assert(eval<"'hello'.rindex('')">().to<int>() == 5);

// ... index/rindex miss with CPython's ValueError
static_assert(eval<"'hello'.index('x')">().exception() == ctpy::ValueError);
static_assert(eval<"'hello'.index('x')">().exception().message() == "substring not found");
static_assert(eval<"'hello'.rindex('x')">().exception() == ctpy::ValueError);
static_assert(eval<"'hello'.rindex('x')">().exception().message() == "substring not found");

// count is non-overlapping; the empty needle matches len+1 slots
static_assert(eval<"'aaa'.count('aa')">().to<int>() == 1);
static_assert(eval<"'banana'.count('an')">().to<int>() == 2);
static_assert(eval<"'banana'.count('x')">().to<int>() == 0);
static_assert(eval<"'abc'.count('')">().to<int>() == 4);
static_assert(eval<"''.count('')">().to<int>() == 1);

static_assert(eval<"'x'.find(5)">().exception() == ctpy::TypeError);
static_assert(eval<"'x'.find(5)">().exception().message()
	== "find() argument 1 must be str, not int");
static_assert(eval<"'x'.rfind(5)">().exception().message()
	== "rfind() argument 1 must be str, not int");
static_assert(eval<"'x'.count(5)">().exception().message()
	== "count() argument 1 must be str, not int");
static_assert(eval<"'x'.find()">().exception().message()
	== "find expected at least 1 argument, got 0");
static_assert(eval<"'x'.count()">().exception().message()
	== "count expected at least 1 argument, got 0");
// documented v0.1 divergence: no start/end slice forms
static_assert(eval<"'x'.find('a', 0)">().exception() == ctpy::TypeError);

// --- startswith / endswith ---------------------------------------------------------------------

static_assert(eval<"'hello'.startswith('he')">().to<bool>());
static_assert(!eval<"'hello'.startswith('lo')">().to<bool>());
static_assert(eval<"'hello'.endswith('lo')">().to<bool>());
static_assert(!eval<"'hello'.endswith('he')">().to<bool>());
static_assert(eval<"'hello'.startswith('')">().to<bool>());
static_assert(eval<"'hello'.endswith('')">().to<bool>());
static_assert(eval<"''.startswith('')">().to<bool>());
static_assert(!eval<"'hi'.startswith('hi there')">().to<bool>()); // longer than the text
static_assert(eval<"'hello'.startswith('hello')">().to<bool>());
static_assert(eval<"'x'.startswith(5)">().exception() == ctpy::TypeError);
static_assert(eval<"'x'.startswith(5)">().exception().message()
	== "startswith first arg must be str or a tuple of str, not int");
static_assert(eval<"'x'.endswith(5)">().exception().message()
	== "endswith first arg must be str or a tuple of str, not int");
static_assert(eval<"'x'.startswith()">().exception().message()
	== "startswith expected at least 1 argument, got 0");
// documented v0.1 divergence: CPython accepts a tuple of prefixes
static_assert(eval<"'x'.startswith(('a', 'x'))">().exception() == ctpy::TypeError);
static_assert(eval<"'x'.endswith(('a', 'x'))">().exception() == ctpy::TypeError);

// --- the is* predicates (ASCII; empty is always False) --------------------------------------------

static_assert(eval<"'123'.isdigit()">().to<bool>());
static_assert(!eval<"'12a'.isdigit()">().to<bool>());
static_assert(!eval<"''.isdigit()">().to<bool>());
static_assert(eval<"'abc'.isalpha()">().to<bool>());
static_assert(eval<"'aBc'.isalpha()">().to<bool>());
static_assert(!eval<"'ab1'.isalpha()">().to<bool>());
static_assert(!eval<"'a b'.isalpha()">().to<bool>());
static_assert(!eval<"''.isalpha()">().to<bool>());
static_assert(eval<"'a1'.isalnum()">().to<bool>());
static_assert(eval<"'123'.isalnum()">().to<bool>());
static_assert(!eval<"'a_1'.isalnum()">().to<bool>());
static_assert(!eval<"''.isalnum()">().to<bool>());
static_assert(eval<R"(' \t\n\r'.isspace())">().to<bool>());
static_assert(!eval<"'a b'.isspace()">().to<bool>());
static_assert(!eval<"''.isspace()">().to<bool>());

// isupper/islower: at least one cased char, every cased char matches
static_assert(eval<"'ABC'.isupper()">().to<bool>());
static_assert(eval<"'A1'.isupper()">().to<bool>());
static_assert(eval<"'A_B'.isupper()">().to<bool>());
static_assert(!eval<"'Ab'.isupper()">().to<bool>());
static_assert(!eval<"'123'.isupper()">().to<bool>()); // no cased char at all
static_assert(!eval<"''.isupper()">().to<bool>());
static_assert(eval<"'abc'.islower()">().to<bool>());
static_assert(eval<"'a1'.islower()">().to<bool>());
static_assert(eval<"'a_b'.islower()">().to<bool>());
static_assert(!eval<"'aB'.islower()">().to<bool>());
static_assert(!eval<"'123'.islower()">().to<bool>());
static_assert(!eval<"''.islower()">().to<bool>());

static_assert(eval<"'x'.isdigit(1)">().exception() == ctpy::TypeError);
static_assert(eval<"'x'.isdigit(1)">().exception().message()
	== "str.isdigit() takes no arguments (1 given)");

// --- zfill (sign-aware) -----------------------------------------------------------------------------

static_assert(eval<"'42'.zfill(5)">().str() == "00042");
static_assert(eval<"'-42'.zfill(5)">().str() == "-0042"); // zeros AFTER the sign
static_assert(eval<"'+1'.zfill(4)">().str() == "+001");
static_assert(eval<"''.zfill(3)">().str() == "000");
static_assert(eval<"'ab'.zfill(1)">().str() == "ab"); // width <= len: unchanged
static_assert(eval<"'-42'.zfill(3)">().str() == "-42");
static_assert(eval<"'-'.zfill(3)">().str() == "-00");
static_assert(eval<"'x'.zfill(-5)">().str() == "x");
static_assert(eval<"'x'.zfill(True)">().str() == "x"); // bool is int-like
static_assert(eval<"'x'.zfill('y')">().exception() == ctpy::TypeError);
static_assert(eval<"'x'.zfill('y')">().exception().message()
	== "'str' object cannot be interpreted as an integer");
static_assert(eval<"'x'.zfill()">().exception().message()
	== "str.zfill() takes exactly one argument (0 given)");
static_assert(eval<"'x'.zfill(1, 2)">().exception().message()
	== "str.zfill() takes exactly one argument (2 given)");

// --- ljust / rjust / center --------------------------------------------------------------------------

static_assert(eval<"'ab'.ljust(5)">().str() == "ab   ");
static_assert(eval<"'ab'.rjust(5)">().str() == "   ab");
static_assert(eval<"'ab'.rjust(5, '*')">().str() == "***ab");
static_assert(eval<"'ab'.ljust(5, '*')">().str() == "ab***");
static_assert(eval<"'ab'.ljust(1)">().str() == "ab"); // width <= len: unchanged
static_assert(eval<"'ab'.rjust(2)">().str() == "ab");

// center: an odd margin leans by CPython's width-parity rule
static_assert(eval<"'ab'.center(5)">().str() == "  ab ");
static_assert(eval<"'ab'.center(5, '*')">().str() == "**ab*");
static_assert(eval<"'abc'.center(6, '*')">().str() == "*abc**");
static_assert(eval<"'a'.center(4, '*')">().str() == "*a**");
static_assert(eval<"'a'.center(2, '*')">().str() == "a*");
static_assert(eval<"''.center(3, '*')">().str() == "***");
static_assert(eval<"'ab'.center(1)">().str() == "ab");
static_assert(eval<"'ab'.center(-1)">().str() == "ab");

// the fill character is validated even when width <= len, like CPython
static_assert(eval<"'ab'.center(6, '')">().exception() == ctpy::TypeError);
static_assert(eval<"'ab'.center(6, 'xy')">().exception().message()
	== "The fill character must be exactly one character long");
static_assert(eval<"'ab'.ljust(2, 'xy')">().exception().message()
	== "The fill character must be exactly one character long");
static_assert(eval<"'ab'.rjust(6, 5)">().exception().message()
	== "The fill character must be a unicode character, not int");
static_assert(eval<"'ab'.ljust('x')">().exception().message()
	== "'str' object cannot be interpreted as an integer");
static_assert(eval<"'x'.center()">().exception().message()
	== "center expected at least 1 argument, got 0");
static_assert(eval<"'x'.ljust(1, 'a', 'b')">().exception().message()
	== "ljust expected at most 2 arguments, got 3");

// --- removeprefix / removesuffix ------------------------------------------------------------------------

static_assert(eval<"'TestHook'.removeprefix('Test')">().str() == "Hook");
static_assert(eval<"'TestHook'.removeprefix('X')">().str() == "TestHook");
static_assert(eval<"'MiscTests'.removesuffix('Tests')">().str() == "Misc");
static_assert(eval<"'MiscTests'.removesuffix('X')">().str() == "MiscTests");
static_assert(eval<"'ab'.removeprefix('')">().str() == "ab");
static_assert(eval<"'ab'.removesuffix('')">().str() == "ab");
static_assert(eval<"'ab'.removeprefix('ab')">().str() == "");
static_assert(eval<"'ab'.removesuffix('ab')">().str() == "");
static_assert(eval<"'ab'.removeprefix(5)">().exception() == ctpy::TypeError);
static_assert(eval<"'ab'.removeprefix(5)">().exception().message()
	== "removeprefix() argument must be str, not int");
static_assert(eval<"'ab'.removesuffix(5)">().exception().message()
	== "removesuffix() argument must be str, not int");
static_assert(eval<"'ab'.removeprefix()">().exception().message()
	== "str.removeprefix() takes exactly one argument (0 given)");

// --- partition / rpartition (always a 3-tuple) ---------------------------------------------------------

static_assert(run<"print('a,b,c'.partition(','))\n">().stdout() == "('a', ',', 'b,c')\n");
static_assert(run<"print('a,b,c'.rpartition(','))\n">().stdout() == "('a,b', ',', 'c')\n");
static_assert(run<"print('abc'.partition(','))\n">().stdout() == "('abc', '', '')\n");
static_assert(run<"print('abc'.rpartition(','))\n">().stdout() == "('', '', 'abc')\n");
static_assert(run<"print(''.partition(','))\n">().stdout() == "('', '', '')\n");
static_assert(eval<"'a,b'.partition(',')">().kind == Kind::tuple);
static_assert(eval<"'key=value'.partition('=')">().result()[0].str() == "key");
static_assert(eval<"'key=value'.partition('=')">().result()[2].str() == "value");
static_assert(eval<"'abc'.partition('')">().exception() == ctpy::ValueError);
static_assert(eval<"'abc'.partition('')">().exception().message() == "empty separator");
static_assert(eval<"'abc'.rpartition('')">().exception().message() == "empty separator");
static_assert(eval<"'abc'.partition(5)">().exception() == ctpy::TypeError);
static_assert(eval<"'abc'.partition(5)">().exception().message() == "must be str, not int");
static_assert(eval<"'abc'.rpartition(5)">().exception().message() == "must be str, not int");
static_assert(eval<"'abc'.partition()">().exception().message()
	== "str.partition() takes exactly one argument (0 given)");

// --- dispatch: unknown names and non-str receivers stay AttributeErrors ---------------------------------

static_assert(eval<"'ab'.frobnicate()">().exception() == ctpy::AttributeError);
static_assert(eval<"'ab'.frobnicate()">().exception().message()
	== "'str' object has no attribute 'frobnicate'");
static_assert(run<R"py(
x = 5
x.split()
)py">().exception() == ctpy::AttributeError);
static_assert(run<R"py(
x = 5
x.split()
)py">().exception().message() == "'int' object has no attribute 'split'");
static_assert(run<"xs = [1]\nxs.upper()\n">().exception() == ctpy::AttributeError);

// --- the methods compose, and work inside programs -------------------------------------------------------

static_assert(eval<"' a,b '.strip().split(',')[1]">().str() == "b");
static_assert(eval<"'-'.join('ab'.upper())">().str() == "A-B");
static_assert(eval<"'Hello World'.lower().replace('world', 'there')">().str()
	== "hello there");
static_assert(eval<"'  42  '.strip().zfill(4)">().str() == "0042");
static_assert(eval<"str(sum([1, 2])).rjust(3, '0')">().str() == "003");

static_assert(run<R"py(
words = 'the quick brown fox'.split()
lengths = []
for w in words:
    lengths.append(len(w))
print(len(words), max(lengths))
print(' | '.join(words))
)py">().stdout() == "4 5\nthe | quick | brown | fox\n");

static_assert(run<R"py(
line = '  name = ada lovelace  '
key, sep, value = line.partition('=')
print(key.strip(), value.strip().title(), sep=': ')
)py">().stdout() == "name: Ada Lovelace\n");

static_assert(run<R"py(
csv = 'a,b,,c'
total = 0
for piece in csv.split(','):
    if piece:
        total += 1
print(total, csv.count(','))
)py">().stdout() == "3 3\n");

// results lift out like any other list of str
static_assert(ctpy::lift<ctc::vector<ctc::string<8>, 4>>(
	run<"parts = 'a,b'.split(',')\n">()["parts"]).size() == 2);

int main() { }
