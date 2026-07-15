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
static_assert(run<R"py(
len = 3
x = len + 1
)py">()["x"].to<int>() == 4);
static_assert(run<R"py(
len = 3
x = len([1])
)py">().exception() == ctpy::TypeError);

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
static_assert(!run<R"py(
xs = [1]
y = xs[5]
)py">().ok());
static_assert(run<R"py(
xs = [1]
y = xs[5]
)py">().exception() == ctpy::IndexError);
static_assert(run<R"py(
xs = [1]
y = xs[5]
)py">().exception().message() == "list index out of range");
static_assert(run<R"py(
xs = [1]
y = xs[-2]
)py">().exception() == ctpy::IndexError);
static_assert(run<R"py(
s = 'ab'
y = s[2]
)py">().exception() == ctpy::IndexError);
static_assert(run<R"py(
s = 'ab'
y = s[2]
)py">().exception().message() == "string index out of range");
static_assert(run<R"py(
t = (1,)
y = t[9]
)py">().exception().message() == "tuple index out of range");

// index type errors, CPython-spelled
static_assert(run<R"py(
xs = [1]
y = xs['a']
)py">().exception() == ctpy::TypeError);
static_assert(run<R"py(
xs = [1]
y = xs['a']
)py">().exception().message()
	== "list indices must be integers or slices, not str");
static_assert(run<R"py(
s = 'ab'
y = s['a']
)py">().exception().message()
	== "string indices must be integers, not 'str'");
static_assert(run<R"py(
x = 5
y = x[0]
)py">().exception() == ctpy::TypeError);
static_assert(run<R"py(
x = 5
y = x[0]
)py">().exception().message()
	== "'int' object is not subscriptable");
static_assert(run<R"py(
x = {1, 2}
y = x[0]
)py">().exception().message()
	== "'set' object is not subscriptable");

// --- dict lookup, KeyError ----------------------------------------------------

static_assert(eval<"{'a': 1, 'b': 2}['b']">().to<int>() == 2);
static_assert(eval<"{1: 'one', 2: 'two'}[2]">().str() == "two");
static_assert(eval<"{True: 'yes'}[1]">().str() == "yes"); // keys compare by value
static_assert(eval<"{'k': 5}.get('k')">().to<int>() == 5);
static_assert(eval<"{'k': 5}.get('z', 99)">().to<int>() == 99);
static_assert(eval<"{'k': 5}.get('z')">().kind == Kind::none);
static_assert(!run<R"py(
d = {'a': 1}
x = d['b']
)py">().ok());
static_assert(run<R"py(
d = {'a': 1}
x = d['b']
)py">().exception() == ctpy::KeyError);
static_assert(run<R"py(
d = {'a': 1}
x = d['b']
)py">().exception().message() == "'b'");
static_assert(run<R"py(
d = {1: 2}
x = d[3]
)py">().exception().message() == "3");

// a duplicate display key keeps its slot, the value updates in place
static_assert(eval<"len({1: 'a', 1: 'b'})">().to<int>() == 1);
static_assert(eval<"{1: 'a', 1: 'b'}[1]">().str() == "b");

// unhashable keys are TypeErrors
static_assert(run<"d = {[1]: 2}">().exception() == ctpy::TypeError);
static_assert(run<"d = {[1]: 2}">().exception().message() == "unhashable type: 'list'");
static_assert(run<"s = {1, [2]}">().exception() == ctpy::TypeError);
static_assert(run<R"py(
d = {}
d[{1: 2}] = 3
)py">().exception() == ctpy::TypeError);
static_assert(run<"d = {(1, [2]): 3}">().exception() == ctpy::TypeError); // deep tuple check
static_assert(run<R"py(
d = {(1, 2): 3}
x = d[(1, 2)]
)py">()["x"].to<int>() == 3);

// --- nesting: a list of dicts ---------------------------------------------------

static_assert(run<R"py(
people = [{'name': 'ada', 'age': 36}, {'name': 'alan', 'age': 41}]
first = people[0]['name']
total = people[0]['age'] + people[1]['age']
)py">()["first"].str() == "ada");
static_assert(run<R"py(
people = [{'name': 'ada', 'age': 36}, {'name': 'alan', 'age': 41}]
total = people[0]['age'] + people[1]['age']
)py">()["total"].to<int>() == 77);

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
static_assert(run<R"py(
xs = [0, 1, 2, 3, 4, 5]
ys = xs[1:4]
n = len(ys)
s = ys[0] + ys[1] + ys[2]
)py">()["n"].to<int>() == 3);
static_assert(run<R"py(
xs = [0, 1, 2, 3, 4, 5]
ys = xs[1:4]
s = ys[0] + ys[1] + ys[2]
)py">()["s"].to<int>() == 6);
static_assert(run<R"py(
xs = [0, 1, 2, 3]
ys = xs[::-1]
y = ys[0]
)py">()["y"].to<int>() == 3);
static_assert(run<R"py(
t = (1, 2, 3)
u = t[1:]
x = u[0]
)py">()["u"].kind == Kind::tuple);
static_assert(run<R"py(
t = (1, 2, 3)
u = t[1:]
x = u[0]
)py">()["x"].to<int>() == 2);
static_assert(eval<"len(range(10)[::2])">().to<int>() == 5);
static_assert(eval<"range(10)[2:8:2][-1]">().to<int>() == 6);
static_assert(eval<"range(10)[::-1][0]">().to<int>() == 9);

// slice misuse
static_assert(run<R"py(
xs = [1]
y = xs[::0]
)py">().exception() == ctpy::ValueError);
static_assert(run<R"py(
xs = [1]
y = xs[::0]
)py">().exception().message() == "slice step cannot be zero");
static_assert(run<R"py(
xs = [1]
y = xs['a':]
)py">().exception() == ctpy::TypeError);
static_assert(run<R"py(
d = {1: 2}
y = d[0:1]
)py">().exception() == ctpy::TypeError);
static_assert(run<R"py(
d = {1: 2}
y = d[0:1]
)py">().exception().message() == "unhashable type: 'slice'");

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

static_assert(run<R"py(
total = 0
for x in [1, 2, 3, 4]:
    total += x
)py">()["total"].to<int>() == 10);
static_assert(run<R"py(
total = 0
for x in (5, 6):
    total += x
)py">()["total"].to<int>() == 11);
static_assert(run<R"py(
total = 0
for x in {1, 2, 3}:
    total += x
)py">()["total"].to<int>() == 6);
static_assert(run<R"py(
d = {'b': 1, 'a': 2, 'c': 3}
order = ''
for k in d:
    order += k
)py">()["order"].str() == "bac"); // dict iterates keys, insertion order
static_assert(run<R"py(
d = {'b': 1, 'a': 2, 'c': 3}
total = 0
for k in d:
    total += d[k]
)py">()["total"].to<int>() == 6);

// dict methods: keys/values/items materialize insertion-ordered lists
static_assert(run<R"py(
d = {'x': 1, 'y': 2}
n = len(d.keys()) + len(d.values()) + len(d.items())
)py">()["n"].to<int>() == 6);
static_assert(run<R"py(
d = {'x': 1, 'y': 2}
k = d.keys()[0]
v = d.values()[1]
)py">()["k"].str() == "x");
static_assert(run<R"py(
d = {'x': 1, 'y': 2}
v = d.values()[1]
)py">()["v"].to<int>() == 2);
static_assert(run<R"py(
d = {'x': 1, 'y': 2}
order = ''
total = 0
for kv in d.items():
    k, v = kv
    order += k
    total += v
)py">()["order"].str() == "xy");
static_assert(run<R"py(
d = {'x': 1, 'y': 2}
total = 0
for kv in d.items():
    k, v = kv
    total += v
)py">()["total"].to<int>() == 3);
static_assert(run<R"py(
d = {'x': 1}
a, b = d.items()[0]
)py">()["a"].str() == "x");
static_assert(run<R"py(
d = {'x': 1}
a, b = d.items()[0]
)py">()["b"].to<int>() == 1);

// --- mutation: append + item stores (the arena realloc pattern) ----------------------

static_assert(run<R"py(
xs = [1, 2]
xs.append(3)
n = len(xs)
last = xs[-1]
)py">()["n"].to<int>() == 3);
static_assert(run<R"py(
xs = [1, 2]
xs.append(3)
last = xs[-1]
)py">()["last"].to<int>() == 3);
static_assert(run<R"py(
xs = []
for i in range(5):
    xs.append(i * i)
y = xs[3]
)py">()["y"].to<int>() == 9);
static_assert(run<R"py(
xs = [1, 2]
xs[0] = 10
y = xs[0]
)py">()["y"].to<int>() == 10);
static_assert(run<R"py(
xs = [1, 2]
xs[-1] = 20
y = xs[1]
)py">()["y"].to<int>() == 20);
static_assert(run<R"py(
m = [[1, 2], [3, 4]]
m[0][1] = 9
y = m[0][1]
)py">()["y"].to<int>() == 9);
static_assert(run<R"py(
xs = [1]
xs[5] = 0
)py">().exception() == ctpy::IndexError);
static_assert(run<R"py(
xs = [1]
xs[5] = 0
)py">().exception().message()
	== "list assignment index out of range");

// dict stores: update in place, new keys append (insertion order kept)
static_assert(run<R"py(
d = {}
d['k'] = 1
v = d['k']
)py">()["v"].to<int>() == 1);
static_assert(run<R"py(
d = {'k': 1}
d['k'] = 2
v = d['k']
n = len(d)
)py">()["v"].to<int>() == 2);
static_assert(run<R"py(
d = {'k': 1}
d['k'] = 2
n = len(d)
)py">()["n"].to<int>() == 1);
static_assert(run<R"py(
d = {'a': 1}
d['b'] = 2
order = ''
for k in d:
    order += k
)py">()["order"].str() == "ab");

// aug-assign through a subscript
static_assert(run<R"py(
d = {'k': 1}
d['k'] += 2
v = d['k']
)py">()["v"].to<int>() == 3);
static_assert(run<R"py(
xs = [1, 2]
xs[0] += 9
y = xs[0]
)py">()["y"].to<int>() == 10);

// tuple/str immutability: TypeError on the soft channel
static_assert(!run<R"py(
t = (1, 2)
t[0] = 5
)py">().ok());
static_assert(run<R"py(
t = (1, 2)
t[0] = 5
)py">().exception() == ctpy::TypeError);
static_assert(run<R"py(
t = (1, 2)
t[0] = 5
)py">().exception().message()
	== "'tuple' object does not support item assignment");
static_assert(run<R"py(
s = 'ab'
s[0] = 'c'
)py">().exception() == ctpy::TypeError);
static_assert(run<R"py(
s = 'ab'
s[0] = 'c'
)py">().exception().message()
	== "'str' object does not support item assignment");

// unknown attributes / methods
static_assert(run<R"py(
xs = [1]
xs.push(2)
)py">().exception() == ctpy::AttributeError);
static_assert(run<R"py(
xs = [1]
xs.push(2)
)py">().exception().message()
	== "'list' object has no attribute 'push'");
static_assert(run<R"py(
t = (1,)
t.append(2)
)py">().exception() == ctpy::AttributeError);
static_assert(run<R"py(
xs = [1]
xs.append(1, 2)
)py">().exception() == ctpy::TypeError);
static_assert(run<R"py(
d = {}
x = d.get()
)py">().exception() == ctpy::TypeError);

int main() { }
