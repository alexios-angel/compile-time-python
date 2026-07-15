#include <ctpy.hpp>

#ifndef CTPY_IN_A_MODULE
#include <type_traits>
#endif

// M1: the pre-lexer. Golden tests: prelexed<SRC> is the marker stream
// (\001 INDENT \002 DEDENT \003 NEWLINE \004 ENDMARKER - written as
// 3-digit octal escapes so no following character can extend them),
// compared against the exact expected rewrite. Failures never
// hard-error; they land in the queryable prelex_diag<SRC> instead.

using ctpy::prelexed;
using ctpy::prelex_diag;
using ctpy::prelex_ok;
using ctpy::prelex_error_kind;

// ---- simple if-block ----------------------------------------------------

static_assert(prelexed<"if x:\n    y = 1\n">
	== "if x:\003\001y = 1\003\002\004");

// ---- nested blocks (indent, dedent mid-stream, dedent at EOF) -----------

static_assert(prelexed<"if a:\n    if b:\n        c = 1\n    d = 2\ne = 3\n">
	== "if a:\003\001if b:\003\001c = 1\003\002d = 2\003\002e = 3\003\004");

// a two-level dedent in the middle of the stream: one marker per level
static_assert(prelexed<"if a:\n    if b:\n        c\nd\n">
	== "if a:\003\001if b:\003\001c\003\002\002d\003\004");

// ---- comments (full-line ones vanish, inline ones are stripped) ---------

static_assert(prelexed<"# top\nx = 1 # tail\n\ny = 2\n">
	== "x = 1 \003y = 2\003\004");

// ---- blank lines inside a block emit nothing at all ---------------------

// blank, whitespace-only, and comment-only lines (at any indentation)
// all vanish without touching the indent stack
static_assert(prelexed<"if x:\n    a = 1\n\n   \t\n  # note\n    b = 2\n">
	== "if x:\003\001a = 1\003b = 2\003\002\004");

// ---- string-literal protection ------------------------------------------

// '#' inside a string is content, not a comment
static_assert(prelexed<"x = '#no'\n"> == "x = '#no'\003\004");

// an escaped quote does not close the literal; the '#' after it is
// still protected
static_assert(prelexed<"x = \"a\\\"b#c\"\n"> == "x = \"a\\\"b#c\"\003\004");

// triple-quoted: real newlines and a '#' survive verbatim, and the
// lines inside the literal never reach the indentation pass
static_assert(prelexed<"s = '''one\ntwo # x\n'''\n">
	== "s = '''one\ntwo # x\n'''\003\004");

// a triple-quoted string inside a block: the block structure around it
// is untouched by the literal's own leading whitespace
static_assert(prelexed<"if x:\n    s = \"\"\"a\n  b\n\"\"\"\n    t = 1\n">
	== "if x:\003\001s = \"\"\"a\n  b\n\"\"\"\003t = 1\003\002\004");

// prefixes r/b/f/rb (any case) are ordinary identifier characters in
// front of the quote; raw strings still shield \" from terminating
static_assert(prelexed<"a = r'\\n'\nb = rb\"x\\\"\"\nc = f'{v}#t'\nd = B'''q\n'''\n">
	== "a = r'\\n'\003b = rb\"x\\\"\"\003c = f'{v}#t'\003d = B'''q\n'''\003\004");

// braces inside an f-string do not open a bracket continuation
static_assert(prelexed<"x = f'{a}'\ny = 1\n">
	== "x = f'{a}'\003y = 1\003\004");

// ---- backslash continuation ----------------------------------------------

static_assert(prelexed<"x = 1 + \\\n2\n"> == "x = 1 +  2\003\004");

// joined lines stay one logical line inside a block
static_assert(prelexed<"if x:\n    y = 1 \\\n+ 2\n">
	== "if x:\003\001y = 1  + 2\003\002\004");

// ---- bracket (implicit) continuation --------------------------------------

static_assert(prelexed<"xs = [1,\n2,\n3]\nn = 0\n">
	== "xs = [1, 2, 3]\003n = 0\003\004");

// a comment inside brackets is stripped, the newline still continues
static_assert(prelexed<"d = {'a': 1, # c\n'b': 2}\n">
	== "d = {'a': 1,  'b': 2}\003\004");

// blank lines inside brackets are just more continuation
static_assert(prelexed<"xs = [\n\n1]\n"> == "xs = [  1]\003\004");

// nested brackets of different kinds
static_assert(prelexed<"t = ([1,\n2],\n{3})\n"> == "t = ([1, 2], {3})\003\004");

// ---- tabs: advance to the next multiple of 8 ------------------------------

static_assert(prelexed<"if x:\n\ty = 1\n\tz = 2\n">
	== "if x:\003\001y = 1\003z = 2\003\002\004");

// a tab (column 8) and eight spaces are the same level
static_assert(prelexed<"if x:\n\ta = 1\n        b = 2\n">
	== "if x:\003\001a = 1\003b = 2\003\002\004");

// space-then-tab also lands on column 8
static_assert(prelexed<"if x:\n \ta = 1\n\tb = 2\n">
	== "if x:\003\001a = 1\003b = 2\003\002\004");

// ---- EOF: NEWLINE synthesis + full DEDENT flush ----------------------------

static_assert(prelexed<"if a:\n    if b:\n        c = 1\n">
	== "if a:\003\001if b:\003\001c = 1\003\002\002\004");

// no trailing newline: the last logical line still ends
static_assert(prelexed<"if a:\n    b = 1">
	== "if a:\003\001b = 1\003\002\004");

// ---- degenerate inputs ------------------------------------------------------

static_assert(prelexed<""> == "\004");
static_assert(prelexed<"\n\n"> == "\004");
static_assert(prelexed<"# only\n# comments\n"> == "\004");

// ---- the queryable error state ----------------------------------------------

static_assert(prelex_ok<"x = 1\n">);
static_assert(prelex_diag<"x = 1\n">.kind == prelex_error_kind::none);
static_assert(prelex_diag<"x = 1\n">.line == 0);

// dedent to column 4, but only 0 and 8 are on the stack
static_assert(!prelex_ok<"if x:\n        a\n    b\n">);
static_assert(prelex_diag<"if x:\n        a\n    b\n">.kind
	== prelex_error_kind::inconsistent_dedent);
static_assert(prelex_diag<"if x:\n        a\n    b\n">.line == 3);
static_assert(!ctpy::prelex_raw<"if x:\n        a\n    b\n">.ok());

// line numbers stay correct across the newlines inside a triple string
static_assert(prelex_diag<"s = '''a\nb'''\nif x:\n        y\n    z\n">.kind
	== prelex_error_kind::inconsistent_dedent);
static_assert(prelex_diag<"s = '''a\nb'''\nif x:\n        y\n    z\n">.line == 5);

// a single-quoted string may not span a line
static_assert(prelex_diag<"x = 'oops\ny = 1\n">.kind
	== prelex_error_kind::unterminated_string);
static_assert(prelex_diag<"x = 'oops\ny = 1\n">.line == 1);

// an unclosed triple-quoted string reports the line it started on
static_assert(prelex_diag<"s = '''ab\ncd\n">.kind
	== prelex_error_kind::unterminated_string);
static_assert(prelex_diag<"s = '''ab\ncd\n">.line == 1);

// ---- the NTTP handoff: oversized -> shrunk -> ctll::fixed_string -------------

// prelexed<> is right-sized: capacity == size, exact type
static_assert(std::is_same_v<decltype(prelexed<"x = 1\n">), const ctc::string<7>>);
static_assert(prelexed<"x = 1\n">.capacity() == prelexed<"x = 1\n">.size());

// to_fixed_string re-encodes a ctc string NTTP for CTLL
static_assert(ctpy::to_fixed_string<ctc::make_string("hi")>() == "hi");

constexpr auto handed_off = ctpy::prelexed_fixed<"x = 1\n">;
static_assert(handed_off.size() == 7);
static_assert(handed_off == "x = 1\003\004");
static_assert(handed_off.correct());

int main() { }
