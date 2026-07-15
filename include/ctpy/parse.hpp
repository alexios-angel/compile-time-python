#ifndef CTPY__PARSE__HPP
#define CTPY__PARSE__HPP

#include "version.hpp"
#include "prelex.hpp"
#include "python.hpp"
#include "actions.hpp"
#include "../ctll/parser.hpp"

// The parse wiring: raw source -> prelex (indentation to markers) ->
// the Tablewright-generated (q)LL(1) table (python.hpp) driven by CTLL
// with the AST-building actions (actions.hpp). Family promise: only
// run<>/parse-demanding entry points may fail the build - is_valid<>
// is a plain bool that is false for any non-parsing input, whether the
// pre-lexer or the parser rejected it.

namespace ctpy {

namespace detail {

// the CTLL parser over the pre-lexed marker stream of Src
template <ctll::fixed_string Src>
using parse_def = ctll::parser<python_grammar, prelexed_fixed<Src>, python_actions>;

// the initial subject: the logical-line counter seeded at the stack
// bottom (the @bump_line hooks advance it; statements are stamped with
// it - the M9 traceback threading, see ast::lined)
using start_context = context<ctll::list<mk::lc<0>>>;

template <ctll::fixed_string Src> consteval bool parse_ok() noexcept {
	if constexpr (!prelex_ok<Src>) {
		return false; // never even instantiate the parser on a bad pre-lex
	} else {
		return parse_def<Src>::template correct_with<start_context>;
	}
}

// unwrapping the final subject: an accepting parse leaves exactly one
// ast::module<...> on the stack
template <typename Ctx> struct module_of;
template <typename M> struct module_of<context<ctll::list<M>>> {
	using type = M;
};

// the parsed module TYPE (ast::module<Stmts...>); only meaningful when
// is_valid<Src> - anything else is a substitution failure, so guard
// uses with is_valid first
template <ctll::fixed_string Src>
using parsed_module =
	typename module_of<typename parse_def<Src>::template output<start_context>::output_type>::type;

} // namespace detail

// does the source pre-lex AND parse? (never a hard error)
CTPY_EXPORT template <ctll::fixed_string Src> inline constexpr bool is_valid = detail::parse_ok<Src>();

namespace detail {

// The family failure policy: parse-demanding entry points (run/eval/
// module/pymodule) hard-error on a non-parsing source with a
// static_assert NAMING THE FAILING STAGE. Each message sits on its own
// static_assert over a plain constexpr bool - a message attached to a
// compound dependent condition can be drowned by the compiler's
// expansion of the expression (the notre-family masking gotcha) - and
// only the stage that failed fires: parse_stage_passed is vacuously
// true when the pre-lex already failed.
template <ctll::fixed_string Src> constexpr bool require_valid() noexcept {
	constexpr bool prelex_stage_passed = prelex_ok<Src>;
	constexpr bool parse_stage_passed = !prelex_stage_passed || is_valid<Src>;
	static_assert(prelex_stage_passed,
		"ctpy: the script failed at the PRELEX stage (inconsistent indentation, an unterminated "
		"string literal, or too-deep nesting) - soft-check with ctpy::is_valid<Src>, inspect with "
		"ctpy::error_info<Src>() / ctpy::error_message<Src>()");
	static_assert(parse_stage_passed,
		"ctpy: the script failed at the PARSE stage (syntax error) - soft-check with "
		"ctpy::is_valid<Src>, inspect with ctpy::error_info<Src>() / ctpy::error_message<Src>()");
	return prelex_stage_passed && parse_stage_passed;
}

} // namespace detail

} // namespace ctpy

#endif
