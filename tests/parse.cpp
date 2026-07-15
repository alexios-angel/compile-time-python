#include <ctpy.hpp>

// M2 checkpoint: the grammar + actions parse the v0.1 subset. Every
// assert is a compile-time parse of real source through prelex ->
// python.hpp (generated (q)LL(1) table) -> CTLL -> the AST actions.
// is_valid<> must stay a soft bool: malformed input makes it false,
// never a compile error.

using ctpy::is_valid;

// --- assignments ------------------------------------------------------

static_assert(is_valid<"x = 1">);
static_assert(is_valid<"x = y = 0">);
static_assert(is_valid<"a, b = 1, 2">);
static_assert(is_valid<"a, b = b, a">);
static_assert(is_valid<"x = 1,">);
static_assert(is_valid<"a, = xs">);
static_assert(is_valid<"x += 1">);
static_assert(is_valid<"total -= 2">);
static_assert(is_valid<"x *= 3">);
static_assert(is_valid<"x /= 4">);
static_assert(is_valid<"x //= 5">);
static_assert(is_valid<"x %= 6">);
static_assert(is_valid<"x **= 2">);
static_assert(is_valid<"x &= y">);
static_assert(is_valid<"x |= y">);
static_assert(is_valid<"x ^= y">);
static_assert(is_valid<"x <<= 1">);
static_assert(is_valid<"x >>= 1">);
static_assert(is_valid<"p.q = 5">);
static_assert(is_valid<"xs[0] = 7">);
static_assert(is_valid<"x=1">);
static_assert(is_valid<"x  =  1">);

// --- expressions, full precedence --------------------------------------

static_assert(is_valid<"1 + 2 * 3">);
static_assert(is_valid<"(1 + 2) * 3">);
static_assert(is_valid<"2 ** 3 ** 2">);
static_assert(is_valid<"-x ** 2">);
static_assert(is_valid<"~x + -y - +z">);
static_assert(is_valid<"a or b and not c">);
static_assert(is_valid<"not not a">);
static_assert(is_valid<"1 < x < 10">);
static_assert(is_valid<"a <= b >= c != d == e">);
static_assert(is_valid<"a in b">);
static_assert(is_valid<"a not in b">);
static_assert(is_valid<"a is b">);
static_assert(is_valid<"a is not b">);
static_assert(is_valid<"x if c else y">);
static_assert(is_valid<"a if b else c if d else e">);
static_assert(is_valid<"a | b ^ c & d << e + f * -g">);
static_assert(is_valid<"5 % 2 // 2">);
static_assert(is_valid<"x or(y)">);
static_assert(is_valid<"not x">);
static_assert(is_valid<"1 if x else 2">);

// --- keyword vs identifier maximal munch --------------------------------

static_assert(is_valid<"iffy = 1">);
static_assert(is_valid<"elsewhere = 2">);
static_assert(is_valid<"formation = 3">);
static_assert(is_valid<"whiley = 4">);
static_assert(is_valid<"breaker = 5">);
static_assert(is_valid<"continued = 6">);
static_assert(is_valid<"passport = 7">);
static_assert(is_valid<"returns = 8">);
static_assert(is_valid<"deft = 9">);
static_assert(is_valid<"notation = 10">);
static_assert(is_valid<"Trueish = 11">);
static_assert(is_valid<"Nones = 12">);
static_assert(is_valid<"Falsey = 13">);
static_assert(is_valid<"info = 14">);
static_assert(is_valid<"x = notation">);
static_assert(is_valid<"x = iffy + 1">);

// --- if / elif / else, nested suites ------------------------------------

static_assert(is_valid<"if x:\n    y = 1\n">);
static_assert(is_valid<"if x: y = 1">);
static_assert(is_valid<"if x:\n    y = 1\nelse:\n    y = 2\n">);
static_assert(is_valid<"if a:\n    x = 1\nelif b:\n    x = 2\nelif c:\n    x = 3\nelse:\n    x = 4\n">);
static_assert(is_valid<
"if a:\n"
"    if b:\n"
"        x = 1\n"
"    else:\n"
"        x = 2\n"
"elif c:\n"
"    x = 3\n"
"else:\n"
"    x = 4\n">);
static_assert(is_valid<"if x:\n    y = 1\nz = 2\n">);
static_assert(is_valid<"if x(1) < 2:\n    pass\n">);
static_assert(is_valid<"if(x):\n    pass\n">);

// --- while / for, break / continue, loop else ----------------------------

static_assert(is_valid<"while x:\n    x -= 1\n">);
static_assert(is_valid<"while x: x -= 1">);
static_assert(is_valid<"while True:\n    break\n">);
static_assert(is_valid<"while x:\n    continue\nelse:\n    y = 1\n">);
static_assert(is_valid<"for i in range(10):\n    total += i\n">);
static_assert(is_valid<"for k, v in items:\n    pass\n">);
static_assert(is_valid<"for a, in pairs:\n    pass\n">);
static_assert(is_valid<"for c in 'abc':\n    print(c)\nelse:\n    pass\n">);
static_assert(is_valid<"for index in xs:\n    pass\n">);

// --- def, params, defaults, return, calls ---------------------------------

static_assert(is_valid<"def f():\n    pass\n">);
static_assert(is_valid<"def f(): pass">);
static_assert(is_valid<"def add(a, b):\n    return a + b\n">);
static_assert(is_valid<"def add(a, b=1):\n    return a + b\n">);
static_assert(is_valid<"def g(n):\n    if n > 0:\n        return n\n    return 0\n">);
static_assert(is_valid<"def h():\n    return\n">);
static_assert(is_valid<"def h():\n    return 1, 2\n">);
static_assert(is_valid<
"def fib(n):\n"
"    a, b = 0, 1\n"
"    for _ in range(n):\n"
"        a, b = b, a + b\n"
"    return a\n"
"\n"
"answer = fib(10)\n">);
static_assert(is_valid<"y = f(1, 2)">);
static_assert(is_valid<"print(\"a\", x, sep=\", \")">);
static_assert(is_valid<"f()">);
static_assert(is_valid<"f(g(x), h(y))">);

// --- indexing, slicing, attributes ---------------------------------------

static_assert(is_valid<"v = xs[0]">);
static_assert(is_valid<"v = xs[i + 1]">);
static_assert(is_valid<"v = xs[1:]">);
static_assert(is_valid<"v = xs[:2]">);
static_assert(is_valid<"v = xs[:]">);
static_assert(is_valid<"v = xs[::2]">);
static_assert(is_valid<"v = xs[1:2:3]">);
static_assert(is_valid<"v = xs[1:2:]">);
static_assert(is_valid<"v = a.b.c">);
static_assert(is_valid<"v = obj.method(1)[0]">);
static_assert(is_valid<"v = (a + b).real">);
static_assert(is_valid<"d[k] = d[k] + 1">);

// --- list / tuple / dict / set displays ------------------------------------

static_assert(is_valid<"t = ()">);
static_assert(is_valid<"t = (1,)">);
static_assert(is_valid<"g = (1 + 2)">);
static_assert(is_valid<"t = (1, 2, 3)">);
static_assert(is_valid<"t = 1, 2, 3">);
static_assert(is_valid<"xs = []">);
static_assert(is_valid<"xs = [1, 2, 3]">);
static_assert(is_valid<"xs = [1, 2, 3,]">);
static_assert(is_valid<"d = {}">);
static_assert(is_valid<"d = {1: 2, 3: 4}">);
static_assert(is_valid<"s = {1, 2}">);
static_assert(is_valid<"d = {\"k\": [1, 2], (1, 2): {3}}">);
static_assert(is_valid<"m = [[1, 2], [3, 4]]">);

// --- strings and f-strings --------------------------------------------------

static_assert(is_valid<"s = 'abc'">);
static_assert(is_valid<"s = \"abc\"">);
static_assert(is_valid<"s = ''">);
static_assert(is_valid<"s = \"\"">);
static_assert(is_valid<"s = 'a\\'b'">);
static_assert(is_valid<"s = \"a\\\"b\"">);
static_assert(is_valid<"doc = '''line1\nline2'''">);
static_assert(is_valid<"doc = \"\"\"a \"quote\" inside\"\"\"">);
static_assert(is_valid<"msg = f\"hi {name}!\"">);
static_assert(is_valid<"msg = f'{a}{b}'">);
static_assert(is_valid<"f\"bare fstring statement\"">);

// --- whole-module shapes ------------------------------------------------------

static_assert(is_valid<"">);
static_assert(is_valid<"# just a comment\n">);
static_assert(is_valid<"\n\n\n">);
static_assert(is_valid<"x = 1  # trailing comment\n">);
static_assert(is_valid<"xs = [1,\n      2]\n">); // bracket continuation
static_assert(is_valid<"x = 1 + \\\n    2\n">);  // backslash continuation

// --- malformed input: is_valid is false, never a hard error -------------------

static_assert(!is_valid<"if x:\n        a = 1\n    b = 2\n">); // inconsistent dedent (prelex)
static_assert(!is_valid<"s = 'abc">);                          // unterminated string (prelex)
static_assert(!is_valid<"x = (1 + 2\n">);                      // unclosed paren
static_assert(!is_valid<"def f(:\n    pass\n">);
static_assert(!is_valid<"x =">);
static_assert(!is_valid<"= 1">);
static_assert(!is_valid<"x + * y">);
static_assert(!is_valid<"if x\n    y = 1\n">);                 // missing colon
static_assert(!is_valid<"else:\n    x = 1\n">);                // stray else
static_assert(!is_valid<"elif x:\n    y = 1\n">);              // stray elif
static_assert(!is_valid<"x ory">);                             // keyword needs a boundary
static_assert(!is_valid<"del x">);                             // reserved word (out of subset)
static_assert(!is_valid<"import os">);                         // reserved word (deferred)
static_assert(!is_valid<"lambda = 1">);                        // reserved word as target
static_assert(!is_valid<"1 +">);
static_assert(!is_valid<"x = = 1">);
static_assert(!is_valid<"f(a=1, 2)">);                         // positional after keyword
static_assert(!is_valid<"v = xs[]">);                          // empty subscript
static_assert(!is_valid<"v = xs[1:2:3:4]">);                   // too many colons
static_assert(!is_valid<"a, b + 1 = 2">);                      // invalid target
static_assert(!is_valid<"f(x) = 1">);                          // invalid target
static_assert(!is_valid<"x += y = 1">);
static_assert(!is_valid<"d = {1: 2, 3}">);                     // mixed dict/set
static_assert(!is_valid<"if x: if y: z = 1">);                 // compound in inline suite
static_assert(!is_valid<"def f(a=1, b):\n    pass\n">);        // non-default after default
static_assert(!is_valid<"for in xs:\n    pass\n">);            // "in" as a target name
static_assert(!is_valid<"if x:\n">);                           // missing suite

// --- extra boundary and superset edges ----------------------------------------

static_assert(is_valid<"x or y">);
static_assert(is_valid<"foo(a, b=1, c=2)">);
static_assert(is_valid<"def f(a, b=1): return a + b">);
static_assert(is_valid<"(a, b) = 1, 2">);
static_assert(is_valid<"x = 10.">);        // "float (basic)": trailing-dot form
static_assert(is_valid<"x = 3.14">);
static_assert(is_valid<"d = {'a': 1}['a']">);
static_assert(is_valid<"x = (((1)))">);
static_assert(!is_valid<"x = 1 if 2">);    // ternary without else
static_assert(!is_valid<"a if b else c = 1">);
static_assert(!is_valid<"x.if = 1">);      // reserved word as attribute
static_assert(!is_valid<"x notin y">);
static_assert(!is_valid<"x not iny">);
static_assert(!is_valid<"x = (1]">);       // mismatched brackets
static_assert(!is_valid<"x = 'a' 'b'">);   // implicit concatenation (out of subset)
static_assert(!is_valid<"True = 1">);
static_assert(!is_valid<"x + 1 = 2">);

// --- AST-shape asserts ----------------------------------------------------------

namespace ast = ctpy::ast;
using ctpy::text;
template <ctll::fixed_string Src> using parsed = ctpy::detail::parsed_module<Src>;

// every statement comes out stamped with its 0-based LOGICAL line
// ordinal (ast::lined<N, Stmt> - the M9 traceback threading)

// x = 1
static_assert(std::is_same_v<
	parsed<"x = 1">,
	ast::module<ast::lined<0,
		ast::assign_stmt<ast::int_lit<text<'1'>>, ast::name<text<'x'>>>>>>);

// x + y*2 : mul binds tighter than add
static_assert(std::is_same_v<
	parsed<"x + y*2">,
	ast::module<ast::lined<0, ast::expr_stmt<
		ast::binary_expr<ast::op_add,
		                 ast::name<text<'x'>>,
		                 ast::binary_expr<ast::op_mul,
		                                  ast::name<text<'y'>>,
		                                  ast::int_lit<text<'2'>>>>>>>>);

// a small if/else (the if is line 0, its body line 1, the else-body
// line 3 - the else header line itself holds no statement)
static_assert(std::is_same_v<
	parsed<"if x:\n    y = 1\nelse:\n    y = 2\n">,
	ast::module<ast::lined<0, ast::if_stmt<
		ast::name<text<'x'>>,
		ast::suite<ast::lined<1, ast::assign_stmt<ast::int_lit<text<'1'>>, ast::name<text<'y'>>>>>,
		ast::clause_pack<>,
		ast::suite<ast::lined<3, ast::assign_stmt<ast::int_lit<text<'2'>>, ast::name<text<'y'>>>>>>>>>);

// a def with one parameter
static_assert(std::is_same_v<
	parsed<"def f(n):\n    return n\n">,
	ast::module<ast::lined<0, ast::def_stmt<
		text<'f'>,
		ast::param_pack<ast::param<text<'n'>, void>>,
		ast::suite<ast::lined<1, ast::return_stmt<ast::name<text<'n'>>>>>>>>>);

// an f-string survives as a raw atom
static_assert(std::is_same_v<
	parsed<"x = f'hi'">,
	ast::module<ast::lined<0,
		ast::assign_stmt<ast::fstr_lit<text<'h', 'i'>>, ast::name<text<'x'>>>>>>);

// chained comparison folds into one compare node
static_assert(std::is_same_v<
	parsed<"1 < x < 10">,
	ast::module<ast::lined<0, ast::expr_stmt<ast::compare_expr<
		ast::int_lit<text<'1'>>,
		ast::cmp_link<ast::op_lt, ast::name<text<'x'>>>,
		ast::cmp_link<ast::op_lt, ast::int_lit<text<'1', '0'>>>>>>>>);

// power is right-associative and folds under unary minus
static_assert(std::is_same_v<
	parsed<"-2 ** 3 ** 2">,
	ast::module<ast::lined<0, ast::expr_stmt<ast::unary_expr<ast::op_neg,
		ast::binary_expr<ast::op_pow,
		                 ast::int_lit<text<'2'>>,
		                 ast::binary_expr<ast::op_pow,
		                                  ast::int_lit<text<'3'>>,
		                                  ast::int_lit<text<'2'>>>>>>>>>);

int main() { }
