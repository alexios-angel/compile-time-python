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

template <ctll::fixed_string Src> consteval bool parse_ok() noexcept {
	if constexpr (!prelex_ok<Src>) {
		return false; // never even instantiate the parser on a bad pre-lex
	} else {
		return parse_def<Src>::template correct_with<context<>>;
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
	typename module_of<typename parse_def<Src>::template output<context<>>::output_type>::type;

} // namespace detail

// does the source pre-lex AND parse? (never a hard error)
CTPY_EXPORT template <ctll::fixed_string Src> inline constexpr bool is_valid = detail::parse_ok<Src>();

} // namespace ctpy

#endif
