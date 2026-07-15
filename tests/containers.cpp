#include <ctpy.hpp>

// M6 checkpoint: containers. list/tuple/dict/set literals, indexing
// (negative indices), slicing with Python's clamp rules, membership,
// len(), iteration, mutation through the append-only arena's realloc
// pattern, and the KeyError/IndexError/TypeError soft channel.

using ctpy::run;
using ctpy::eval;
using ctpy::Kind;

// --- literals and len() ---------------------------------------------------

static_assert(eval<"len([1, 2, 3])">().to<int>() == 3);
static_assert(eval<"len((1, 2))">().to<int>() == 2);
static_assert(eval<"len({'a': 1, 'b': 2})">().to<int>() == 2);
static_assert(eval<"len('hello')">().to<int>() == 5);
static_assert(eval<"len(range(10))">().to<int>() == 10);
static_assert(eval<"len([])">().to<int>() == 0);
static_assert(eval<"len(())">().to<int>() == 0);
static_assert(eval<"len({})">().to<int>() == 0);          // {} is a dict
static_assert(eval<"len({1, 2, 2, 3, 1})">().to<int>() == 3); // sets dedupe
static_assert(eval<"len(5)">().exception() == ctpy::TypeError);
static_assert(eval<"len(5)">().exception().message() == "object of type 'int' has no len()");
static_assert(run<"x = [1, 2]">()["x"].kind == Kind::list);
static_assert(run<"x = (1, 2)">()["x"].kind == Kind::tuple);
static_assert(run<"x = {1, 2}">()["x"].kind == Kind::set);
static_assert(run<"x = {1: 2}">()["x"].kind == Kind::dict);

// a bound name shadows the builtin (and is then not callable)
static_assert(run<"len = 3\nx = len + 1\n">()["x"].to<int>() == 4);
static_assert(run<"len = 3\nx = len([1])\n">().exception() == ctpy::TypeError);

// --- indexing, negative indices --------------------------------------------

static_assert(eval<"[10, 20, 30][0]">().to<int>() == 10);
static_assert(eval<"[10, 20, 30][2]">().to<int>() == 30);
static_assert(eval<"[10, 20, 30][-1]">().to<int>() == 30);
static_assert(eval<"[10, 20, 30][-3]">().to<int>() == 10);
static_assert(eval<"(1, 2, 3)[-2]">().to<int>() == 2);
static_assert(eval<"'hello'[1]">().str() == "e");
static_assert(eval<"'hello'[-1]">().str() == "o");
static_assert(eval<"range(10)[3]">().to<int>() == 3);
static_assert(eval<"range(10)[-1]">().to<int>() == 9);
static_assert(eval<"range(4, 20, 3)[2]">().to<int>() == 10);
static_assert(eval<"[10, 20][True]">().to<int>() == 20); // bool is int-like
static_assert(eval<"[[1, 2], [3, 4]][1][0]">().to<int>() == 3);

// IndexError rides the soft channel - never a build failure
static_assert(!run<"xs = [1]\ny = xs[5]\n">().ok());
static_assert(run<"xs = [1]\ny = xs[5]\n">().exception() == ctpy::IndexError);
static_assert(run<"xs = [1]\ny = xs[5]\n">().exception().message() == "list index out of range");
static_assert(run<"xs = [1]\ny = xs[-2]\n">().exception() == ctpy::IndexError);
static_assert(run<"s = 'ab'\ny = s[2]\n">().exception() == ctpy::IndexError);
static_assert(run<"s = 'ab'\ny = s[2]\n">().exception().message() == "string index out of range");
static_assert(run<"t = (1,)\ny = t[9]\n">().exception().message() == "tuple index out of range");

// index type errors, CPython-spelled
static_assert(run<"xs = [1]\ny = xs['a']\n">().exception() == ctpy::TypeError);
static_assert(run<"xs = [1]\ny = xs['a']\n">().exception().message()
	== "list indices must be integers or slices, not str");
static_assert(run<"s = 'ab'\ny = s['a']\n">().exception().message()
	== "string indices must be integers, not 'str'");
static_assert(run<"x = 5\ny = x[0]\n">().exception() == ctpy::TypeError);
static_assert(run<"x = 5\ny = x[0]\n">().exception().message()
	== "'int' object is not subscriptable");
static_assert(run<"x = {1, 2}\ny = x[0]\n">().exception().message()
	== "'set' object is not subscriptable");

// --- dict lookup, KeyError ----------------------------------------------------

static_assert(eval<"{'a': 1, 'b': 2}['b']">().to<int>() == 2);
static_assert(eval<"{1: 'one', 2: 'two'}[2]">().str() == "two");
static_assert(eval<"{True: 'yes'}[1]">().str() == "yes"); // keys compare by value
static_assert(eval<"{'k': 5}.get('k')">().to<int>() == 5);
static_assert(eval<"{'k': 5}.get('z', 99)">().to<int>() == 99);
static_assert(eval<"{'k': 5}.get('z')">().kind == Kind::none);
static_assert(!run<"d = {'a': 1}\nx = d['b']\n">().ok());
static_assert(run<"d = {'a': 1}\nx = d['b']\n">().exception() == ctpy::KeyError);
static_assert(run<"d = {'a': 1}\nx = d['b']\n">().exception().message() == "'b'");
static_assert(run<"d = {1: 2}\nx = d[3]\n">().exception().message() == "3");

// a duplicate display key keeps its slot, the value updates in place
static_assert(eval<"len({1: 'a', 1: 'b'})">().to<int>() == 1);
static_assert(eval<"{1: 'a', 1: 'b'}[1]">().str() == "b");

// unhashable keys are TypeErrors
static_assert(run<"d = {[1]: 2}">().exception() == ctpy::TypeError);
static_assert(run<"d = {[1]: 2}">().exception().message() == "unhashable type: 'list'");
static_assert(run<"s = {1, [2]}">().exception() == ctpy::TypeError);
static_assert(run<"d = {}\nd[{1: 2}] = 3\n">().exception() == ctpy::TypeError);
static_assert(run<"d = {(1, [2]): 3}">().exception() == ctpy::TypeError); // deep tuple check
static_assert(run<"d = {(1, 2): 3}\nx = d[(1, 2)]\n">()["x"].to<int>() == 3);

// --- nesting: a list of dicts ---------------------------------------------------

static_assert(run<
"people = [{'name': 'ada', 'age': 36}, {'name': 'alan', 'age': 41}]\n"
"first = people[0]['name']\n"
"total = people[0]['age'] + people[1]['age']\n">()["first"].str() == "ada");
static_assert(run<
"people = [{'name': 'ada', 'age': 36}, {'name': 'alan', 'age': 41}]\n"
"total = people[0]['age'] + people[1]['age']\n">()["total"].to<int>() == 77);

// --- slicing: clamps, steps, empties ----------------------------------------------

static_assert(eval<"'hello world'[0:5]">().str() == "hello");
static_assert(eval<"'hello'[1:]">().str() == "ello");
static_assert(eval<"'hello'[:3]">().str() == "hel");
static_assert(eval<"'hello'[:]">().str() == "hello");
static_assert(eval<"'hello'[::-1]">().str() == "olleh");
static_assert(eval<"'abcdef'[::2]">().str() == "ace");
static_assert(eval<"'abcdef'[1::2]">().str() == "bdf");
static_assert(eval<"'abcdef'[-4:-1]">().str() == "cde");
static_assert(eval<"'abcdef'[4:1:-1]">().str() == "edc");
static_assert(eval<"'hello'[1:100]">().str() == "ello");   // out-of-range clamps
static_assert(eval<"'hello'[-100:2]">().str() == "he");
static_assert(eval<"'hello'[10:20]">().str() == "");       // empty, not an error
static_assert(eval<"'hello'[3:1]">().str() == "");
static_assert(eval<"''[::-1]">().str() == "");
static_assert(eval<"'hello'[None:2]">().str() == "he");    // None = default bound

// list/tuple slices copy element runs; range slices stay lazy ranges
static_assert(run<
"xs = [0, 1, 2, 3, 4, 5]\n"
"ys = xs[1:4]\n"
"n = len(ys)\n"
"s = ys[0] + ys[1] + ys[2]\n">()["n"].to<int>() == 3);
static_assert(run<
"xs = [0, 1, 2, 3, 4, 5]\n"
"ys = xs[1:4]\n"
"s = ys[0] + ys[1] + ys[2]\n">()["s"].to<int>() == 6);
static_assert(run<"xs = [0, 1, 2, 3]\nys = xs[::-1]\ny = ys[0]\n">()["y"].to<int>() == 3);
static_assert(run<"t = (1, 2, 3)\nu = t[1:]\nx = u[0]\n">()["u"].kind == Kind::tuple);
static_assert(run<"t = (1, 2, 3)\nu = t[1:]\nx = u[0]\n">()["x"].to<int>() == 2);
static_assert(eval<"len(range(10)[::2])">().to<int>() == 5);
static_assert(eval<"range(10)[2:8:2][-1]">().to<int>() == 6);
static_assert(eval<"range(10)[::-1][0]">().to<int>() == 9);

// slice misuse
static_assert(run<"xs = [1]\ny = xs[::0]\n">().exception() == ctpy::ValueError);
static_assert(run<"xs = [1]\ny = xs[::0]\n">().exception().message() == "slice step cannot be zero");
static_assert(run<"xs = [1]\ny = xs['a':]\n">().exception() == ctpy::TypeError);
static_assert(run<"d = {1: 2}\ny = d[0:1]\n">().exception() == ctpy::TypeError);
static_assert(run<"d = {1: 2}\ny = d[0:1]\n">().exception().message() == "unhashable type: 'slice'");

// --- membership -----------------------------------------------------------------

static_assert(eval<"2 in [1, 2, 3]">().to<bool>());
static_assert(eval<"4 in [1, 2, 3]">().to<bool>() == false);
static_assert(eval<"4 not in [1, 2, 3]">().to<bool>());
static_assert(eval<"2 in (1, 2)">().to<bool>());
static_assert(eval<"2 in {1, 2}">().to<bool>());
static_assert(eval<"'a' in {'a': 1}">().to<bool>());       // dict membership is KEYS
static_assert(eval<"1 in {'a': 1}">().to<bool>() == false);
static_assert(eval<"'ell' in 'hello'">().to<bool>());
static_assert(eval<"[1, 2] in [[1, 2], [3]]">().to<bool>()); // deep equality
static_assert(eval<"True in [1]">().to<bool>());            // numeric cross-kind
static_assert(run<"x = [1] in {1: 2}">().exception() == ctpy::TypeError); // unhashable needle

// container equality: lists/tuples ordered, sets/dicts order-insensitive
static_assert(eval<"[1, 2] == [1, 2]">().to<bool>());
static_assert(eval<"[1, 2] == [2, 1]">().to<bool>() == false);
static_assert(eval<"{1, 2} == {2, 1}">().to<bool>());
static_assert(eval<"{1, 2} == {1, 3}">().to<bool>() == false);
static_assert(eval<"{'a': 1, 'b': 2} == {'b': 2, 'a': 1}">().to<bool>());
static_assert(eval<"{'a': 1} == {'a': 2}">().to<bool>() == false);
static_assert(eval<"[1, 2] == (1, 2)">().to<bool>() == false); // kinds never mix

// --- iteration (insertion order) ---------------------------------------------------

static_assert(run<
"total = 0\n"
"for x in [1, 2, 3, 4]:\n"
"    total += x\n">()["total"].to<int>() == 10);
static_assert(run<
"total = 0\n"
"for x in (5, 6):\n"
"    total += x\n">()["total"].to<int>() == 11);
static_assert(run<
"total = 0\n"
"for x in {1, 2, 3}:\n"
"    total += x\n">()["total"].to<int>() == 6);
static_assert(run<
"d = {'b': 1, 'a': 2, 'c': 3}\n"
"order = ''\n"
"for k in d:\n"
"    order += k\n">()["order"].str() == "bac"); // dict iterates keys, insertion order
static_assert(run<
"d = {'b': 1, 'a': 2, 'c': 3}\n"
"total = 0\n"
"for k in d:\n"
"    total += d[k]\n">()["total"].to<int>() == 6);

// dict methods: keys/values/items materialize insertion-ordered lists
static_assert(run<
"d = {'x': 1, 'y': 2}\n"
"n = len(d.keys()) + len(d.values()) + len(d.items())\n">()["n"].to<int>() == 6);
static_assert(run<
"d = {'x': 1, 'y': 2}\n"
"k = d.keys()[0]\n"
"v = d.values()[1]\n">()["k"].str() == "x");
static_assert(run<
"d = {'x': 1, 'y': 2}\n"
"v = d.values()[1]\n">()["v"].to<int>() == 2);
static_assert(run<
"d = {'x': 1, 'y': 2}\n"
"order = ''\n"
"total = 0\n"
"for kv in d.items():\n"
"    k, v = kv\n"
"    order += k\n"
"    total += v\n">()["order"].str() == "xy");
static_assert(run<
"d = {'x': 1, 'y': 2}\n"
"total = 0\n"
"for kv in d.items():\n"
"    k, v = kv\n"
"    total += v\n">()["total"].to<int>() == 3);
static_assert(run<"d = {'x': 1}\na, b = d.items()[0]\n">()["a"].str() == "x");
static_assert(run<"d = {'x': 1}\na, b = d.items()[0]\n">()["b"].to<int>() == 1);

// --- mutation: append + item stores (the arena realloc pattern) ----------------------

static_assert(run<
"xs = [1, 2]\n"
"xs.append(3)\n"
"n = len(xs)\n"
"last = xs[-1]\n">()["n"].to<int>() == 3);
static_assert(run<
"xs = [1, 2]\n"
"xs.append(3)\n"
"last = xs[-1]\n">()["last"].to<int>() == 3);
static_assert(run<
"xs = []\n"
"for i in range(5):\n"
"    xs.append(i * i)\n"
"y = xs[3]\n">()["y"].to<int>() == 9);
static_assert(run<"xs = [1, 2]\nxs[0] = 10\ny = xs[0]\n">()["y"].to<int>() == 10);
static_assert(run<"xs = [1, 2]\nxs[-1] = 20\ny = xs[1]\n">()["y"].to<int>() == 20);
static_assert(run<"m = [[1, 2], [3, 4]]\nm[0][1] = 9\ny = m[0][1]\n">()["y"].to<int>() == 9);
static_assert(run<"xs = [1]\nxs[5] = 0\n">().exception() == ctpy::IndexError);
static_assert(run<"xs = [1]\nxs[5] = 0\n">().exception().message()
	== "list assignment index out of range");

// dict stores: update in place, new keys append (insertion order kept)
static_assert(run<"d = {}\nd['k'] = 1\nv = d['k']\n">()["v"].to<int>() == 1);
static_assert(run<"d = {'k': 1}\nd['k'] = 2\nv = d['k']\nn = len(d)\n">()["v"].to<int>() == 2);
static_assert(run<"d = {'k': 1}\nd['k'] = 2\nn = len(d)\n">()["n"].to<int>() == 1);
static_assert(run<
"d = {'a': 1}\n"
"d['b'] = 2\n"
"order = ''\n"
"for k in d:\n"
"    order += k\n">()["order"].str() == "ab");

// aug-assign through a subscript
static_assert(run<"d = {'k': 1}\nd['k'] += 2\nv = d['k']\n">()["v"].to<int>() == 3);
static_assert(run<"xs = [1, 2]\nxs[0] += 9\ny = xs[0]\n">()["y"].to<int>() == 10);

// tuple/str immutability: TypeError on the soft channel
static_assert(!run<"t = (1, 2)\nt[0] = 5\n">().ok());
static_assert(run<"t = (1, 2)\nt[0] = 5\n">().exception() == ctpy::TypeError);
static_assert(run<"t = (1, 2)\nt[0] = 5\n">().exception().message()
	== "'tuple' object does not support item assignment");
static_assert(run<"s = 'ab'\ns[0] = 'c'\n">().exception() == ctpy::TypeError);
static_assert(run<"s = 'ab'\ns[0] = 'c'\n">().exception().message()
	== "'str' object does not support item assignment");

// unknown attributes / methods
static_assert(run<"xs = [1]\nxs.push(2)\n">().exception() == ctpy::AttributeError);
static_assert(run<"xs = [1]\nxs.push(2)\n">().exception().message()
	== "'list' object has no attribute 'push'");
static_assert(run<"t = (1,)\nt.append(2)\n">().exception() == ctpy::AttributeError);
static_assert(run<"xs = [1]\nxs.append(1, 2)\n">().exception() == ctpy::TypeError);
static_assert(run<"d = {}\nx = d.get()\n">().exception() == ctpy::TypeError);

int main() { }
