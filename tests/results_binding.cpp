#include <ctpy.hpp>

// M8 checkpoint: results and binding. The interim result plumbing is
// gone - a run flattens the reachable globals out of the dead arena,
// right-sizes them via ctc::shrunk into per-Src static storage, and
// hands out uniform null-object ctpy::value views. C++ seeds Python
// through arg<>/file<>/stdin_text<>/pymodule<> descriptors; Python
// values lift back out through views and ctpy::lift<> into ctc
// containers. Every README snippet is mirrored here VERBATIM first -
// the README must never claim something this suite does not pin.

using ctpy::run;
using ctpy::eval;
using ctpy::Kind;

// === the README, snippet by snippet ==========================================

namespace readme_fib {

constexpr auto out = ctpy::run<R"py(
def fib(n):
    a, b = 0, 1
    for _ in range(n):
        a, b = b, a + b
    return a

answer = fib(10)
print("fib(10) =", answer)
)py">();

static_assert(out.ok());                         // no Python exception
static_assert(out.stdout() == "fib(10) = 55\n"); // print() -> captured stdout
static_assert(out["answer"].to<int>() == 55);    // globals are readable

} // namespace readme_fib

namespace readme_views {

constexpr auto cfg = ctpy::run<"cfg = {'name': 'ctpy', 'dims': [3, 4]}\n">();
static_assert(cfg["cfg"]["name"].str() == "ctpy"); // views chain to any depth
static_assert(cfg["cfg"]["dims"][1].to<int>() == 4);
static_assert(!cfg["cfg"]["nope"][0].exists());    // misses chain harmlessly

} // namespace readme_views

namespace readme_lift {

constexpr auto sorted_out = ctpy::run<"xs = sorted([3, 1, 4, 1, 5])\n">();
constexpr auto xs = ctpy::lift<ctc::vector<int, 16>>(sorted_out["xs"]);
static_assert(xs.size() == 5 && xs[0] == 1 && xs[4] == 5);

// ... then ctc::shrunk as usual: capacity 16 right-sizes to 5
constexpr auto tight = ctc::shrunk<xs>();
static_assert(tight.capacity() == 5 && tight[2] == 3);

} // namespace readme_lift

namespace readme_args {

constexpr auto r = ctpy::run<"total = sum(values) * factor\n">(
        ctpy::arg<"values">(ctc::vector<int, 3>{1, 2, 3}),
        ctpy::arg<"factor">(10));
static_assert(r["total"].to<int>() == 60);

} // namespace readme_args

namespace readme_sugar {

static_assert(ctpy::eval<"2 ** 10">().to<int>() == 1024);

constexpr auto fib = ctpy::module<R"py(
def fib(n):
    a, b = 0, 1
    for _ in range(n):
        a, b = b, a + b
    return a
)py">;
static_assert(fib.call<"fib">(20).to<int>() == 6765);

} // namespace readme_sugar

namespace readme_vfs {

constexpr auto io = ctpy::run<R"py(
data = open("config.txt").read()
)py">(ctpy::file<"config.txt", "timeout=250\n">);
static_assert(io["data"].str() == "timeout=250\n");

constexpr auto fed = ctpy::run<"who = input()\n">(ctpy::stdin_text<"world\n">);
static_assert(fed["who"].str() == "world");

} // namespace readme_vfs

// === ctpy::value: the uniform null-object view ================================

namespace views {

constexpr auto out = run<R"py(
answer = 6 * 7
pi = 3.5
flag = True
nothing = None
name = 'ada'
xs = [1, [2, 3], 'hi']
t = (10, 20)
s = {9}
d = {'a': {'b': [10, 20]}, 1: 'one'}
r = range(2, 12, 3)
def f():
    return 1
)py">();

// kinds and scalar conversions
static_assert(out["answer"].kind == Kind::int_ && out["answer"].to<long long>() == 42);
static_assert(out["pi"].kind == Kind::float_ && out["pi"].to<double>() == 3.5);
static_assert(out["pi"].to<int>() == 3); // float -> int narrows like a cast
static_assert(out["flag"].kind == Kind::boolean && out["flag"].to<bool>());
static_assert(out["nothing"].kind == Kind::none && out["nothing"].exists());
static_assert(out["name"].kind == Kind::str && out["name"].str() == "ada");
static_assert(out["name"].to<std::string_view>() == "ada");
static_assert(out["xs"].kind == Kind::list && out["t"].kind == Kind::tuple);
static_assert(out["s"].kind == Kind::set && out["d"].kind == Kind::dict);
static_assert(out["r"].kind == Kind::range && out["f"].kind == Kind::function);

// misses are harmless null objects, bound-None is NOT a miss
static_assert(!out["never"].exists() && out["never"].kind == Kind::none);
static_assert(!out["never"]["deep"][0]["deeper"].exists()); // chains forever
static_assert(out["nothing"].exists());

// Python truthiness through to<bool>
static_assert(!out["nothing"].to<bool>());
static_assert(out["name"].to<bool>() && out["xs"].to<bool>());
static_assert(!out["never"].to<bool>());

// sizes (Python len)
static_assert(out["name"].size() == 3 && out["xs"].size() == 3);
static_assert(out["t"].size() == 2 && out["s"].size() == 1);
static_assert(out["d"].size() == 2 && out["r"].size() == 4);
static_assert(out["answer"].size() == 0 && out["answer"].empty());

// positional subscripts: negative counts from the end, past-the-end is
// a null view, a set is not subscriptable (all Python semantics)
static_assert(out["xs"][0].to<int>() == 1);
static_assert(out["xs"][1][0].to<int>() == 2);
static_assert(out["xs"][-1].str() == "hi");
static_assert(out["t"][-2].to<int>() == 10);
static_assert(!out["xs"][3].exists() && !out["xs"][-4].exists());
static_assert(!out["s"][0].exists());

// str and range index like Python sequences
static_assert(out["name"][0].str() == "a" && out["name"][-1].str() == "a");
static_assert(out["name"][1].str() == "d");
static_assert(out["r"][0].to<int>() == 2 && out["r"][3].to<int>() == 11);
static_assert(out["r"][-1].to<int>() == 11 && !out["r"][4].exists());

// dict lookup: str keys by name, int keys by VALUE (d[1] is a KEY
// lookup, never an index - Python semantics carry into the views)
static_assert(out["d"]["a"]["b"][1].to<int>() == 20);
static_assert(out["d"][1].str() == "one");
static_assert(!out["d"]["b"].exists() && !out["d"][2].exists());

// begin/end iteration: list elements, str characters, range ints,
// dict KEYS (insertion order) - all as fresh value views
constexpr long long xs_sum = [] {
	long long total = 0;
	for (ctpy::value v : out["d"]["a"]["b"]) {
		total += v.to<long long>();
	}
	return total;
}();
static_assert(xs_sum == 30);

constexpr auto spell = [](const ctpy::value & v) {
	ctc::string<8> word{};
	for (ctpy::value unit : v) {
		word.append(unit.str());
	}
	return word;
};
static_assert(spell(out["name"]) == "ada");

constexpr long long r_sum = [] {
	long long total = 0;
	for (ctpy::value v : out["r"]) {
		total += v.to<long long>();
	}
	return total;
}();
static_assert(r_sum == 2 + 5 + 8 + 11);

constexpr bool dict_iterates_keys = [] {
	std::size_t at = 0;
	bool good = true;
	for (ctpy::value key : out["d"]) {
		good = good && (at != 0 || key.str() == "a");
		good = good && (at != 1 || key.to<int>() == 1);
		++at;
	}
	return good && at == 2;
}();
static_assert(dict_iterates_keys);

// globals(): every binding, in insertion order
static_assert(out.globals().size() == 11);
constexpr bool globals_iterate = [] {
	std::size_t at = 0;
	bool good = true;
	for (ctpy::global_view g : out.globals()) {
		good = good && (at != 0 || (g.name == "answer" && g.val.to<int>() == 42));
		good = good && (at != 4 || (g.name == "name" && g.val.str() == "ada"));
		++at;
	}
	return good && at == 11;
}();
static_assert(globals_iterate);

// deep aliasing survives the flatten: both names see the same content
constexpr auto aliased = run<R"py(
a = [1, {'k': 2}]
b = a
)py">();
static_assert(aliased["b"][1]["k"].to<int>() == 2);
static_assert(aliased["a"][1]["k"].to<int>() == aliased["b"][1]["k"].to<int>());

// a raising script still exposes stdout and the pre-raise globals
constexpr auto raised = run<R"py(
x = 41
print('pre')
y = 1 // 0
z = 5
)py">();
static_assert(!raised.ok() && raised.exception() == ctpy::ZeroDivisionError);
static_assert(raised.stdout() == "pre\n");
static_assert(raised["x"].to<int>() == 41 && !raised["z"].exists());

} // namespace views

// === arg<>: C++ seeds Python globals ==========================================

namespace args_in {

// scalars
static_assert(run<"y = x * 2\n">(ctpy::arg<"x">(21))["y"].to<int>() == 42);
static_assert(run<"y = x / 2\n">(ctpy::arg<"x">(3.5))["y"].to<double>() == 1.75);
static_assert(run<"y = not x\n">(ctpy::arg<"x">(false))["y"].to<bool>());
static_assert(run<"t = type_ok\n">(ctpy::arg<"type_ok">(true))["t"].kind == Kind::boolean);

// string literals and views become Python str
static_assert(run<"s = name + '!'\n">(ctpy::arg<"name">("hi"))["s"].str() == "hi!");
static_assert(run<"n = len(s)\n">(ctpy::arg<"s">(std::string_view{"abcd"}))["n"].to<int>() == 4);
static_assert(run<"u = s\n">(ctpy::arg<"s">("x"))["u"].kind == Kind::str);

// ctc containers lift in: vector -> list, string -> str, map -> dict
static_assert(run<"n = len(xs)\n">(
	ctpy::arg<"xs">(ctc::vector<int, 4>{5, 6, 7}))["n"].to<int>() == 3);
static_assert(run<"m = max(xs)\n">(
	ctpy::arg<"xs">(ctc::vector<double, 3>{1.5, 2.5}))["m"].to<double>() == 2.5);
static_assert(run<"s2 = s * 2\n">(
	ctpy::arg<"s">(ctc::string<8>{"ab"}))["s2"].str() == "abab");
static_assert(run<"v = m[2]\n">(
	ctpy::arg<"m">(ctc::map<int, int, 4>{{1, 10}, {2, 20}}))["v"].to<int>() == 20);
static_assert(run<"v = m['b']\n">(
	ctpy::arg<"m">(ctc::map<ctc::string<4>, int, 4>{{ctc::string<4>{"a"}, 1},
	                                                {ctc::string<4>{"b"}, 2}}))["v"].to<int>() == 2);

// nested containers nest as Python nests
static_assert(run<"v = grid[1][0]\n">(
	ctpy::arg<"grid">(ctc::vector<ctc::vector<int, 2>, 2>{{1, 2}, {3, 4}}))["v"].to<int>() == 3);

// seeds behave like ordinary globals: the script may shadow them, and
// several seeds bind left to right
static_assert(run<"x = x + 1\n">(ctpy::arg<"x">(1))["x"].to<int>() == 2);
constexpr auto pair = run<"z = a * b\n">(ctpy::arg<"a">(6), ctpy::arg<"b">(7));
static_assert(pair["z"].to<int>() == 42 && pair["a"].to<int>() == 6);

} // namespace args_in

// === lift<>: Python values OUT into ctc containers ============================

namespace lift_out {

constexpr auto out = run<R"py(
xs = [4, 5, 6]
fs = (0.5, 1.5)
s = 'lift me'
r = range(3)
d = {1: 10, 2: 20}
nested = [[1], [2, 3]]
)py">();

constexpr auto xs = ctpy::lift<ctc::vector<long long, 8>>(out["xs"]);
static_assert(xs.size() == 3 && xs[0] == 4 && xs[2] == 6);

constexpr auto fs = ctpy::lift<ctc::vector<double, 4>>(out["fs"]);
static_assert(fs.size() == 2 && fs[1] == 1.5);

constexpr auto s = ctpy::lift<ctc::string<16>>(out["s"]);
static_assert(s == "lift me");

constexpr auto r = ctpy::lift<ctc::vector<int, 4>>(out["r"]); // ranges materialize
static_assert(r.size() == 3 && r[2] == 2);

constexpr auto d = ctpy::lift<ctc::map<int, int, 4>>(out["d"]);
static_assert(d.size() == 2 && d.at(1) == 10 && d.at(2) == 20);

constexpr auto nested = ctpy::lift<ctc::vector<ctc::vector<int, 4>, 4>>(out["nested"]);
static_assert(nested.size() == 2 && nested[1][1] == 3);

// a miss (or a kind mismatch) lifts as EMPTY, never an error
static_assert(ctpy::lift<ctc::vector<int, 4>>(out["never"]).empty());
static_assert(ctpy::lift<ctc::string<4>>(out["xs"]).empty());
static_assert(ctpy::lift<ctc::map<int, int, 4>>(out["xs"]).empty());

} // namespace lift_out

// === file<>: the compile-time VFS behind open() ===============================

namespace vfs {

// unmounted: OSError, spelled like CPython's FileNotFoundError
static_assert(run<"f = open('nope.txt')\n">().exception() == ctpy::OSError);
static_assert(run<"f = open('nope.txt')\n">().exception().message()
	== "[Errno 2] No such file or directory: 'nope.txt'");

// mounted: open() succeeds, read() hands the contents over
constexpr auto io = run<R"py(
f = open('a.txt')
first = f.read()
second = f.read()
other = open('b.txt').read()
)py">(
	ctpy::file<"a.txt", "alpha\n">, ctpy::file<"b.txt", "beta">);
static_assert(io.ok());
static_assert(io["f"].kind == Kind::file);
static_assert(io["first"].str() == "alpha\n");
static_assert(io["second"].str() == ""); // a second read() is exhausted, like CPython
static_assert(io["other"].str() == "beta");

// the wrong-path spelling still raises even with OTHER files mounted
static_assert(run<"open('c.txt')\n">(ctpy::file<"a.txt", "alpha\n">).exception()
	== ctpy::OSError);

// bad arguments spell CPython's TypeError (documented deviation: real
// CPython would accept an int as a file DESCRIPTOR - there are none at
// compile time, so a non-str path always raises); the two-argument
// mode form is a soft ctpy-spelled v0.1 error
static_assert(run<"open(3)\n">().exception() == ctpy::TypeError);
static_assert(run<"open(3)\n">().exception().message()
	== "expected str, bytes or os.PathLike object, not int");
static_assert(run<"open('a', 'w')\n">().exception() == ctpy::TypeError);

} // namespace vfs

// === stdin_text<>: input() ====================================================

namespace stdin_feed {

constexpr auto fed = run<R"py(
a = input()
b = input('? ')
c = input()
)py">(ctpy::stdin_text<"one\ntwo\nlast">);
static_assert(fed.ok());
static_assert(fed["a"].str() == "one");   // the newline is consumed, not kept
static_assert(fed["b"].str() == "two");
static_assert(fed["c"].str() == "last");  // an unterminated final line still reads
static_assert(fed.stdout() == "? ");      // the prompt printed, no newline

// reading past the end (or with nothing mounted) is CPython's EOFError
static_assert(run<"input()\n">().exception() == ctpy::EOFError);
static_assert(run<"input()\n">().exception().message() == "EOF when reading a line");
static_assert(run<R"py(
input()
input()
)py">(ctpy::stdin_text<"only\n">).exception() == ctpy::EOFError);
static_assert(run<"input(1, 2)\n">().exception() == ctpy::TypeError);

} // namespace stdin_feed

// === pymodule<>: the registry descriptor (shape only in v0.1) =================

namespace registry {

// mounting is accepted by run<> and validates the source parses;
// import execution is deferred, so the descriptor is inert for now
constexpr auto shaped = run<"x = 1\n">(
	ctpy::pymodule<"helpers", R"py(
def h():
    return 1
)py">);
static_assert(shaped.ok() && shaped["x"].to<int>() == 1);
static_assert(ctpy::pymodule<"helpers", R"py(
def h():
    return 1
)py">.name() == "helpers");

} // namespace registry

// === module<src>.call<"fn">(args...) ==========================================

namespace modules {

constexpr auto lib = ctpy::module<R"py(
def double(x):
    return 2 * x
def greet(name):
    return 'hello ' + name
def loud(xs):
    return len(xs) * 10
def noisy():
    print('called')
def boom():
    return 1 // 0
counter = 0
)py">;

// arguments lift like arg<> payloads; the return value is result()
static_assert(lib.call<"double">(21).to<int>() == 42);
static_assert(lib.call<"double">(2.5).to<double>() == 5.0);
static_assert(lib.call<"greet">("ada").str() == "hello ada");
static_assert(lib.call<"loud">(ctc::vector<int, 3>{7, 8, 9}).to<int>() == 30);
static_assert(lib.call<"double">(21).kind == Kind::int_);

// module state is visible around the call: globals + stdout ride along
static_assert(lib.call<"noisy">().stdout() == "called\n");
static_assert(lib.call<"double">(1)["counter"].to<int>() == 0);

// a def returning a container comes back as a chaining view
constexpr auto rows = ctpy::module<R"py(
def table(n):
    return [[n, n * 2], 'done']
)py">;
static_assert(rows.call<"table">(3).result()[0][1].to<int>() == 6);
static_assert(rows.call<"table">(3).result()[-1].str() == "done");

// failures are values, spelled like the interpreter everywhere else
static_assert(lib.call<"boom">().exception() == ctpy::ZeroDivisionError);
static_assert(!lib.call<"boom">().ok());
static_assert(lib.call<"missing">().exception() == ctpy::NameError);
static_assert(ctpy::module<"x = 5\n">.call<"x">().exception() == ctpy::TypeError);
static_assert(ctpy::module<"x = 5\n">.call<"x">().exception().message()
	== "'int' object is not callable");
static_assert(lib.call<"double">(1, 2).exception() == ctpy::TypeError);
static_assert(lib.call<"double">(1, 2).exception().message()
	== "double() takes 1 positional argument but 2 were given");

// a module whose BODY raises reports it from any call
static_assert(ctpy::module<R"py(
y = 1 // 0
def f():
    return 1
)py">.call<"f">().exception() == ctpy::ZeroDivisionError);

} // namespace modules

// === eval<>: sugar over the same machinery ====================================

namespace eval_sugar {

static_assert(eval<"2 ** 10">().to<int>() == 1024);
static_assert(eval<"'py' * 2">().str() == "pypy");
static_assert(eval<"3.5 / 2">().kind == Kind::float_);
static_assert(eval<"range(5)">().kind == Kind::range);

// the expression value is the result() channel, a full chaining view
static_assert(eval<"[1, 2, 3]">().result()[2].to<int>() == 3);
static_assert(eval<"{'k': (1, 2)}">().result()["k"][1].to<int>() == 2);
static_assert(eval<"sorted('cab')">().result()[0].str() == "a");

// a raising expression is a value, not a build failure
static_assert(!eval<"1 // 0">().ok());
static_assert(eval<"1 // 0">().exception() == ctpy::ZeroDivisionError);
static_assert(eval<"len(5)">().exception() == ctpy::TypeError);

} // namespace eval_sugar

int main() { }
