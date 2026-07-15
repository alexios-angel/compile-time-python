#include <ctpy.hpp>

#include <string_view>

// M9 diagnostics: structured error_info<Src>() / rendered
// error_message<Src>() for scripts that fail to pre-lex or parse (both
// soft - only run/eval/module hard-error, naming the stage), and
// Python traceback LINE NUMBERS on the exceptions of scripts that run
// but raise. Negative tests assert structured FIELDS (stage/kind/
// line/column), never compiler output text.

using ctpy::error_info;
using ctpy::error_kind;
using ctpy::error_message;
using ctpy::error_stage;
using ctpy::is_valid;

// --- a valid script reports nothing --------------------------------------------------

static_assert(is_valid<"x = 1\n">);
static_assert(error_info<"x = 1\n">().ok());
static_assert(error_info<"x = 1\n">().stage == error_stage::none);
static_assert(error_info<"x = 1\n">().kind == error_kind::none);
static_assert(error_message<"x = 1\n">().empty());

// --- prelex failures: stage, kind, line, column ---------------------------------------

// a dedent to a depth that was never indented to is CPython's
// IndentationError; it surfaces from the PRELEX stage with the line
// and column of the offending statement. (Raw sources in this file are
// ANCHORED against the opening delimiter - no leading blank line - so
// the asserted line/column numbers stay exactly what a user would see.)
inline constexpr auto bad_dedent = ctll::fixed_string{R"py(if x:
        a
    b
)py"};
static_assert(!is_valid<bad_dedent>);
static_assert(!error_info<bad_dedent>().ok());
static_assert(error_info<bad_dedent>().stage == error_stage::prelex);
static_assert(error_info<bad_dedent>().kind == error_kind::inconsistent_dedent);
static_assert(error_info<bad_dedent>().line == 3);
static_assert(error_info<bad_dedent>().column == 5);
// ... and it agrees with the pre-lexer's own status line
static_assert(ctpy::prelex_diag<bad_dedent>.line == 3);

// an unterminated string fails at its OPENING quote's line/column
inline constexpr auto unterminated = ctll::fixed_string{R"py(x = 'oops
y = 1
)py"};
static_assert(!is_valid<unterminated>);
static_assert(error_info<unterminated>().stage == error_stage::prelex);
static_assert(error_info<unterminated>().kind == error_kind::unterminated_string);
static_assert(error_info<unterminated>().line == 1);
static_assert(error_info<unterminated>().column == 5);

// --- parse failures: position mapped back through the marker stream --------------------

inline constexpr auto dangling_eq = ctll::fixed_string{"x = \n"};
static_assert(!is_valid<dangling_eq>);
static_assert(error_info<dangling_eq>().stage == error_stage::parse);
static_assert(error_info<dangling_eq>().kind == error_kind::syntax);
static_assert(error_info<dangling_eq>().line == 1);
static_assert(error_info<dangling_eq>().column == 5);

// the failure position lands mid-line, after the dangling operator
inline constexpr auto dangling_plus = ctll::fixed_string{"y = 1 +\n"};
static_assert(error_info<dangling_plus>().stage == error_stage::parse);
static_assert(error_info<dangling_plus>().line == 1);
static_assert(error_info<dangling_plus>().column == 8);

// ... and on the RIGHT physical line of a multi-line script
inline constexpr auto second_line_bad = ctll::fixed_string{R"py(x = 1
y = (]
)py"};
static_assert(!is_valid<second_line_bad>);
static_assert(error_info<second_line_bad>().stage == error_stage::parse);
static_assert(error_info<second_line_bad>().line == 2);

// a reserved word as a plain name is rejected semantically (parse stage)
static_assert(error_info<"lambda = 1\n">().stage == error_stage::parse);
static_assert(error_info<"lambda = 1\n">().line == 1);

// --- error_message: stage + caret rendered into static storage -------------------------

constexpr bool contains(std::string_view text, std::string_view needle) noexcept {
	return text.find(needle) != std::string_view::npos;
}

// the exact rendering, pinned once (source line + caret under column 5)
static_assert(error_message<unterminated>() ==
	"ctpy: prelex error (SyntaxError) at line 1, column 5: unterminated string literal\n"
	"  x = 'oops\n"
	"      ^");

// every message names its stage and carries the caret
static_assert(contains(error_message<bad_dedent>(), "prelex"));
static_assert(contains(error_message<bad_dedent>(), "IndentationError"));
static_assert(contains(error_message<bad_dedent>(), "line 3"));
static_assert(contains(error_message<bad_dedent>(), "^"));
static_assert(contains(error_message<dangling_eq>(), "parse"));
static_assert(contains(error_message<dangling_eq>(), "SyntaxError"));
static_assert(contains(error_message<dangling_eq>(), "^"));

// --- tracebacks: raising scripts carry the line of the raising statement ---------------

using ctpy::run;

// the milestone checkpoint
static_assert(run<"1/0\n">().exception().type == ctpy::ex_kind::ZeroDivisionError);
static_assert(run<"1/0\n">().exception() == ctpy::ZeroDivisionError);
static_assert(run<"1/0\n">().exception().line == 1);
static_assert(run<"1/0\n">().exception().message() == "division by zero");

// the raising statement's own line, not the script's last
static_assert(run<R"py(x = 1
y = 1 // 0
)py">().exception().line == 2);
static_assert(run<R"py(x = 1
y = 1 // 0
)py">().exception() == ctpy::ZeroDivisionError);

// the README failure-policy snippet, verbatim
constexpr auto boom = ctpy::run<R"py(x = 1
y = x // 0
)py">();
static_assert(!boom.ok());
static_assert(boom.exception() == ctpy::ZeroDivisionError);
static_assert(boom.exception().message() == "division by zero");
static_assert(boom.exception().line == 2);  // like a traceback would say

// a raise inside a function reports the line INSIDE the def
static_assert(run<R"py(def f():
    return 1 // 0

f()
)py">().exception().line == 2);

// a wrong-arity call reports the CALL line
static_assert(run<R"py(def f(x):
    return x

f()
)py">().exception() == ctpy::TypeError);
static_assert(run<R"py(def f(x):
    return x

f()
)py">().exception().line == 4);

// a while TEST that raises on a LATER iteration still reports the
// while line (loop headers re-stamp per evaluation)
static_assert(run<R"py(n = 2
while 10 // n:
    n -= 1
)py">().exception() == ctpy::ZeroDivisionError);
static_assert(run<R"py(n = 2
while 10 // n:
    n -= 1
)py">().exception().line == 2);

// an elif clause carries its OWN line
static_assert(run<R"py(x = 1
if x == 0:
    pass
elif 1 // 0:
    pass
)py">().exception().line == 4);

// a non-iterable for reports the for line
static_assert(run<"for i in 5:\n    pass\n">().exception() == ctpy::TypeError);
static_assert(run<"for i in 5:\n    pass\n">().exception().line == 1);

// LOGICAL lines: a statement continued across physical lines (bracket
// continuation) reports the line it STARTED on, like CPython
static_assert(run<R"py(z = (1
     // 0)
)py">().exception().line == 1);
// ... and continuation before a raise does not shift later lines
static_assert(run<R"py(x = (1 +
     2)
y = 1 // 0
)py">().exception().line == 3);

// an inline suite shares its header's line
static_assert(run<"if True: x = 1 // 0\n">().exception().line == 1);

// eval<> stamps its single expression as line 1
static_assert(ctpy::eval<"1 // 0">().exception() == ctpy::ZeroDivisionError);
static_assert(ctpy::eval<"1 // 0">().exception().line == 1);

// a clean run keeps line 0 in the (empty) exception channel
static_assert(run<"x = 1\n">().exception().line == 0);
static_assert(run<"x = 1\n">().exception().type == ctpy::ex_kind::none);

int main() { }
