#ifndef CTPY__ACTIONS__HPP
#define CTPY__ACTIONS__HPP

#include "version.hpp"
#include "text.hpp"
#include "ast.hpp"
#include "../ctll/list.hpp"
#include "../ctll/grammars.hpp"
#include "python.hpp"

#ifndef CTPY_IN_A_MODULE
#include <string_view>
#include <type_traits>
#endif

// Semantic actions building the type-level Python AST while CTLL walks
// the generated (q)LL(1) table (the ctlark context<>/text<>/mk::*
// architecture). The grammar recognizes a flat operand/operator soup;
// the PRECEDENCE lives here: every binary operator action folds the
// pending-marker stack down to its own precedence before pushing its
// marker (Pratt folding on a type stack), so "x + y*2" folds mul
// before add and "2**3**2" stays right-associative. Everything the
// grammar could not decide structurally is decided here from stack
// markers: tuple-vs-paren (comma markers above pmark), dict-vs-set,
// index-vs-slice (colon count), call kwargs, assignment-target
// validity, reserved words used as names, and elif/else attachment to
// the statement on top of the stack. Malformed structure folds to
// ctll::reject, which fails the PARSE (is_valid<> stays a soft false,
// never a compile error) - never a C++ exception, never a hard error.

namespace ctpy {

// the parser subject: a stack of partial results
CTPY_EXPORT template <typename Stack = ctll::list<>> struct context {
	using stack_type = Stack;
	constexpr context() noexcept { }
	constexpr context(Stack) noexcept { }
};

template <typename... Content> context(ctll::list<Content...>) -> context<ctll::list<Content...>>;

// parse-time stack markers
namespace mk {

template <typename Op> struct bop { };                 // pending binary operator (left operand below)
template <typename Op> struct uop { };                 // pending unary operator
struct t_if { };                                       // ternary: then-part below, condition being parsed
template <typename Then, typename Cond> struct t_pair { }; // ternary: waiting for the else-part
struct smark { };                                      // string literal under construction
struct fmark { };                                      // f-string under construction
struct pmark { };                                      // ( paren/tuple display
struct lmark { };                                      // [ list display
struct brmark { };                                     // { dict/set display
template <typename Callee> struct cmark { };           // ( call arguments
template <typename Obj> struct imark { };              // [ subscript
struct cm { };                                         // element separator (comma)
struct sc { };                                         // slice colon
struct dmark { };                                      // dict colon (key below)
template <typename NameText> struct kwmark { };        // name= (kwarg or param default)
template <typename Target> struct amark { };           // assignment: validated target below
template <typename Op, typename Target> struct gmark { }; // augmented assignment
struct retm { };                                       // return statement
struct ifm { };                                        // if header
struct elifm { };                                      // elif header (attachable if below)
struct elsem { };                                      // else header (attachable statement below)
struct whilem { };                                     // while header
struct form { };                                       // for header (targets follow)
template <typename Target> struct inm { };             // for header: folded targets, iterable follows
struct defm { };                                       // def statement
struct parm { };                                       // parameter list
struct sm { };                                         // suite: statements accumulate above

} // namespace mk

namespace detail {

// --- classification -------------------------------------------------

template <typename T> struct is_expr_t : std::false_type { };
template <typename X> struct is_expr_t<ast::name<X>> : std::true_type { };
template <typename X> struct is_expr_t<ast::int_lit<X>> : std::true_type { };
template <typename X> struct is_expr_t<ast::float_lit<X>> : std::true_type { };
template <typename X> struct is_expr_t<ast::str_lit<X>> : std::true_type { };
template <typename X> struct is_expr_t<ast::fstr_lit<X>> : std::true_type { };
template <> struct is_expr_t<ast::none_lit> : std::true_type { };
template <> struct is_expr_t<ast::true_lit> : std::true_type { };
template <> struct is_expr_t<ast::false_lit> : std::true_type { };
template <typename... Es> struct is_expr_t<ast::tuple_expr<Es...>> : std::true_type { };
template <typename... Es> struct is_expr_t<ast::list_expr<Es...>> : std::true_type { };
template <typename... Es> struct is_expr_t<ast::set_expr<Es...>> : std::true_type { };
template <typename... Is> struct is_expr_t<ast::dict_expr<Is...>> : std::true_type { };
template <typename O, typename E> struct is_expr_t<ast::unary_expr<O, E>> : std::true_type { };
template <typename O, typename L, typename R> struct is_expr_t<ast::binary_expr<O, L, R>> : std::true_type { };
template <typename L, typename... Ls> struct is_expr_t<ast::compare_expr<L, Ls...>> : std::true_type { };
template <typename C, typename T, typename E> struct is_expr_t<ast::ternary_expr<C, T, E>> : std::true_type { };
template <typename F, typename... As> struct is_expr_t<ast::call_expr<F, As...>> : std::true_type { };
template <typename O, typename I> struct is_expr_t<ast::subscript_expr<O, I>> : std::true_type { };
template <typename O, typename N> struct is_expr_t<ast::attribute_expr<O, N>> : std::true_type { };
template <typename T> inline constexpr bool is_expr = is_expr_t<T>::value;

template <typename T> struct is_kwarg_t : std::false_type { };
template <typename N, typename V> struct is_kwarg_t<ast::kwarg<N, V>> : std::true_type { };
template <typename T> inline constexpr bool is_kwarg = is_kwarg_t<T>::value;

template <typename T> struct is_dict_item_t : std::false_type { };
template <typename K, typename V> struct is_dict_item_t<ast::dict_item<K, V>> : std::true_type { };
template <typename T> inline constexpr bool is_dict_item = is_dict_item_t<T>::value;

// a gatherable element of a comma-separated display/list
template <typename T> inline constexpr bool is_element = is_expr<T> || is_kwarg<T> || is_dict_item<T>;

template <typename T> struct is_name_t : std::false_type { };
template <typename X> struct is_name_t<ast::name<X>> : std::true_type { };
template <typename T> inline constexpr bool is_name_node = is_name_t<T>::value;

template <typename T> struct is_stmt_t : std::false_type { };
template <typename E> struct is_stmt_t<ast::expr_stmt<E>> : std::true_type { };
template <typename V, typename... Tg> struct is_stmt_t<ast::assign_stmt<V, Tg...>> : std::true_type { };
template <typename O, typename T, typename V> struct is_stmt_t<ast::aug_stmt<O, T, V>> : std::true_type { };
template <> struct is_stmt_t<ast::pass_stmt> : std::true_type { };
template <> struct is_stmt_t<ast::break_stmt> : std::true_type { };
template <> struct is_stmt_t<ast::continue_stmt> : std::true_type { };
template <typename E> struct is_stmt_t<ast::return_stmt<E>> : std::true_type { };
template <typename T, typename B, typename P, typename E> struct is_stmt_t<ast::if_stmt<T, B, P, E>> : std::true_type { };
template <typename T, typename B, typename E> struct is_stmt_t<ast::while_stmt<T, B, E>> : std::true_type { };
template <typename T, typename I, typename B, typename E> struct is_stmt_t<ast::for_stmt<T, I, B, E>> : std::true_type { };
template <typename N, typename P, typename B> struct is_stmt_t<ast::def_stmt<N, P, B>> : std::true_type { };
template <typename T> inline constexpr bool is_stmt = is_stmt_t<T>::value;

// statements allowed as an INLINE suite ("if x: y = 1")
template <typename T> struct is_simple_stmt_t : std::false_type { };
template <typename E> struct is_simple_stmt_t<ast::expr_stmt<E>> : std::true_type { };
template <typename V, typename... Tg> struct is_simple_stmt_t<ast::assign_stmt<V, Tg...>> : std::true_type { };
template <typename O, typename T, typename V> struct is_simple_stmt_t<ast::aug_stmt<O, T, V>> : std::true_type { };
template <> struct is_simple_stmt_t<ast::pass_stmt> : std::true_type { };
template <> struct is_simple_stmt_t<ast::break_stmt> : std::true_type { };
template <> struct is_simple_stmt_t<ast::continue_stmt> : std::true_type { };
template <typename E> struct is_simple_stmt_t<ast::return_stmt<E>> : std::true_type { };
template <typename T> inline constexpr bool is_simple_stmt = is_simple_stmt_t<T>::value;

// valid assignment targets (recursively through tuple/list displays)
template <typename T> struct is_target_t : std::false_type { };
template <typename X> struct is_target_t<ast::name<X>> : std::true_type { };
template <typename O, typename N> struct is_target_t<ast::attribute_expr<O, N>> : std::true_type { };
template <typename O, typename I> struct is_target_t<ast::subscript_expr<O, I>> : std::true_type { };
template <typename... Es> struct is_target_t<ast::tuple_expr<Es...>> {
	static constexpr bool value = (is_target_t<Es>::value && ...);
};
template <typename... Es> struct is_target_t<ast::list_expr<Es...>> {
	static constexpr bool value = (is_target_t<Es>::value && ...);
};
template <typename T> inline constexpr bool is_target = is_target_t<T>::value;

// augmented assignment takes a single non-display target
template <typename T> struct is_simple_target_t : std::false_type { };
template <typename X> struct is_simple_target_t<ast::name<X>> : std::true_type { };
template <typename O, typename N> struct is_simple_target_t<ast::attribute_expr<O, N>> : std::true_type { };
template <typename O, typename I> struct is_simple_target_t<ast::subscript_expr<O, I>> : std::true_type { };
template <typename T> inline constexpr bool is_simple_target = is_simple_target_t<T>::value;

// --- reserved words --------------------------------------------------

constexpr bool is_reserved(std::string_view s) noexcept {
	constexpr std::string_view words[]{
		"False", "None", "True", "and", "as", "assert", "async", "await",
		"break", "class", "continue", "def", "del", "elif", "else",
		"except", "finally", "for", "from", "global", "if", "import",
		"in", "is", "lambda", "nonlocal", "not", "or", "pass", "raise",
		"return", "try", "while", "with", "yield"};
	for (auto w : words) {
		if (s == w) {
			return true;
		}
	}
	return false;
}

// --- operator precedence (Python, bigger binds tighter) --------------

template <typename Op> inline constexpr int prec_of = 0;
template <> inline constexpr int prec_of<ast::op_or> = 2;
template <> inline constexpr int prec_of<ast::op_and> = 3;
template <> inline constexpr int prec_of<ast::op_not> = 4;
template <> inline constexpr int prec_of<ast::op_lt> = 5;
template <> inline constexpr int prec_of<ast::op_le> = 5;
template <> inline constexpr int prec_of<ast::op_gt> = 5;
template <> inline constexpr int prec_of<ast::op_ge> = 5;
template <> inline constexpr int prec_of<ast::op_eq> = 5;
template <> inline constexpr int prec_of<ast::op_ne> = 5;
template <> inline constexpr int prec_of<ast::op_in> = 5;
template <> inline constexpr int prec_of<ast::op_not_in> = 5;
template <> inline constexpr int prec_of<ast::op_is> = 5;
template <> inline constexpr int prec_of<ast::op_is_not> = 5;
template <> inline constexpr int prec_of<ast::op_bor> = 6;
template <> inline constexpr int prec_of<ast::op_bxor> = 7;
template <> inline constexpr int prec_of<ast::op_band> = 8;
template <> inline constexpr int prec_of<ast::op_shl> = 9;
template <> inline constexpr int prec_of<ast::op_shr> = 9;
template <> inline constexpr int prec_of<ast::op_add> = 10;
template <> inline constexpr int prec_of<ast::op_sub> = 10;
template <> inline constexpr int prec_of<ast::op_mul> = 11;
template <> inline constexpr int prec_of<ast::op_div> = 11;
template <> inline constexpr int prec_of<ast::op_floordiv> = 11;
template <> inline constexpr int prec_of<ast::op_mod> = 11;
template <> inline constexpr int prec_of<ast::op_neg> = 12;
template <> inline constexpr int prec_of<ast::op_pos> = 12;
template <> inline constexpr int prec_of<ast::op_invert> = 12;
template <> inline constexpr int prec_of<ast::op_pow> = 13;

template <typename Op> inline constexpr bool right_assoc_of = false;
template <> inline constexpr bool right_assoc_of<ast::op_pow> = true;

template <typename Op> struct is_cmp_t : std::false_type { };
template <> struct is_cmp_t<ast::op_lt> : std::true_type { };
template <> struct is_cmp_t<ast::op_le> : std::true_type { };
template <> struct is_cmp_t<ast::op_gt> : std::true_type { };
template <> struct is_cmp_t<ast::op_ge> : std::true_type { };
template <> struct is_cmp_t<ast::op_eq> : std::true_type { };
template <> struct is_cmp_t<ast::op_ne> : std::true_type { };
template <> struct is_cmp_t<ast::op_in> : std::true_type { };
template <> struct is_cmp_t<ast::op_not_in> : std::true_type { };
template <> struct is_cmp_t<ast::op_is> : std::true_type { };
template <> struct is_cmp_t<ast::op_is_not> : std::true_type { };
template <typename Op> inline constexpr bool is_cmp = is_cmp_t<Op>::value;

// push one item onto a stack
template <typename H, typename... Ts> constexpr auto prepended(ctll::list<Ts...>) noexcept {
	return ctll::list<H, Ts...>{};
}

// folding one binary step; comparisons chain CPython-style
template <typename Op, typename L, typename R> struct fold_cmp2 {
	using type = ast::compare_expr<L, ast::cmp_link<Op, R>>;
};
template <typename Op, typename L0, typename... Ls, typename R>
struct fold_cmp2<Op, ast::compare_expr<L0, Ls...>, R> {
	using type = ast::compare_expr<L0, Ls..., ast::cmp_link<Op, R>>;
};
template <typename Op, typename L, typename R>
using folded_bin = typename std::conditional_t<is_cmp<Op>, fold_cmp2<Op, L, R>,
                                               std::type_identity<ast::binary_expr<Op, L, R>>>::type;

// --- folding pending operators --------------------------------------

// forward declarations: the overloads recurse into one another, and a
// dependent unqualified call only sees declarations above it
template <int P, bool Right, typename... Ts>
constexpr auto fold_ops(ctll::list<Ts...> s) noexcept;
template <int P, bool Right, typename R, typename Op, typename L, typename... Ts>
constexpr auto fold_ops(ctll::list<R, mk::bop<Op>, L, Ts...> s) noexcept;
template <int P, bool Right, typename E, typename Op, typename... Ts>
constexpr auto fold_ops(ctll::list<E, mk::uop<Op>, Ts...> s) noexcept;
template <typename... Ts> constexpr auto fold_all(ctll::list<Ts...> s) noexcept;
template <typename R, typename Op, typename L, typename... Ts>
constexpr auto fold_all(ctll::list<R, mk::bop<Op>, L, Ts...> s) noexcept;
template <typename E, typename Op, typename... Ts>
constexpr auto fold_all(ctll::list<E, mk::uop<Op>, Ts...> s) noexcept;
template <typename E, typename Th, typename Co, typename... Ts>
constexpr auto fold_all(ctll::list<E, mk::t_pair<Th, Co>, Ts...> s) noexcept;

// fold pending operators of precedence >= P (or > P when the incoming
// operator is right-associative); stops at any non-operator marker
template <int P, bool Right, typename... Ts>
constexpr auto fold_ops(ctll::list<Ts...> s) noexcept {
	return s;
}
template <int P, bool Right, typename R, typename Op, typename L, typename... Ts>
constexpr auto fold_ops(ctll::list<R, mk::bop<Op>, L, Ts...> s) noexcept {
	if constexpr (is_expr<R> && is_expr<L> && (Right ? (prec_of<Op> > P) : (prec_of<Op> >= P))) {
		return fold_ops<P, Right>(ctll::list<folded_bin<Op, L, R>, Ts...>{});
	} else {
		return s;
	}
}
template <int P, bool Right, typename E, typename Op, typename... Ts>
constexpr auto fold_ops(ctll::list<E, mk::uop<Op>, Ts...> s) noexcept {
	if constexpr (is_expr<E> && (Right ? (prec_of<Op> > P) : (prec_of<Op> >= P))) {
		return fold_ops<P, Right>(ctll::list<ast::unary_expr<Op, E>, Ts...>{});
	} else {
		return s;
	}
}

// fold everything foldable on top: binaries, unaries, closed ternaries
template <typename... Ts> constexpr auto fold_all(ctll::list<Ts...> s) noexcept {
	return s;
}
template <typename R, typename Op, typename L, typename... Ts>
constexpr auto fold_all(ctll::list<R, mk::bop<Op>, L, Ts...> s) noexcept {
	if constexpr (is_expr<R> && is_expr<L>) {
		return fold_all(ctll::list<folded_bin<Op, L, R>, Ts...>{});
	} else {
		return s;
	}
}
template <typename E, typename Op, typename... Ts>
constexpr auto fold_all(ctll::list<E, mk::uop<Op>, Ts...> s) noexcept {
	if constexpr (is_expr<E>) {
		return fold_all(ctll::list<ast::unary_expr<Op, E>, Ts...>{});
	} else {
		return s;
	}
}
template <typename E, typename Th, typename Co, typename... Ts>
constexpr auto fold_all(ctll::list<E, mk::t_pair<Th, Co>, Ts...> s) noexcept {
	if constexpr (is_expr<E>) {
		return fold_all(ctll::list<ast::ternary_expr<Co, Th, E>, Ts...>{});
	} else {
		return s;
	}
}

// fold a completed dict pair or kwarg value
template <typename... Ts> constexpr auto fold_pair(ctll::list<Ts...> s) noexcept {
	return s;
}
template <typename V, typename K, typename... Ts>
constexpr auto fold_pair(ctll::list<V, mk::dmark, K, Ts...> s) noexcept {
	if constexpr (is_expr<V> && is_expr<K>) {
		return ctll::list<ast::dict_item<K, V>, Ts...>{};
	} else {
		return s;
	}
}
template <typename V, typename N, typename... Ts>
constexpr auto fold_pair(ctll::list<V, mk::kwmark<N>, Ts...> s) noexcept {
	if constexpr (is_expr<V>) {
		return ctll::list<ast::kwarg<N, V>, Ts...>{};
	} else {
		return s;
	}
}

template <typename... Ts> constexpr auto fold_ready(ctll::list<Ts...> s) noexcept {
	return fold_pair(fold_all(s));
}

// --- gathering comma-separated elements ------------------------------

template <typename... Es> struct epack { };
template <typename Pack, bool Trail, typename Rest> struct gathered { };

template <bool Trail, typename... Acc, typename... Ts>
constexpr auto gather_loop(epack<Acc...>, ctll::list<Ts...>) noexcept {
	return gathered<epack<Acc...>, Trail, ctll::list<Ts...>>{};
}
template <bool Trail, typename... Acc, typename E, typename... Ts>
constexpr auto gather_loop(epack<Acc...>, ctll::list<E, Ts...>) noexcept {
	if constexpr (is_element<E>) {
		return gathered<epack<E, Acc...>, Trail, ctll::list<Ts...>>{};
	} else {
		return gathered<epack<Acc...>, Trail, ctll::list<E, Ts...>>{};
	}
}
template <bool Trail, typename... Acc, typename E, typename... Ts>
constexpr auto gather_loop(epack<Acc...>, ctll::list<E, mk::cm, Ts...>) noexcept {
	if constexpr (is_element<E>) {
		return gather_loop<Trail>(epack<E, Acc...>{}, ctll::list<Ts...>{});
	} else {
		return gathered<epack<Acc...>, Trail, ctll::list<E, mk::cm, Ts...>>{};
	}
}

template <typename... Ts> constexpr auto gather_elems(ctll::list<Ts...> s) noexcept {
	return gather_loop<false>(epack<>{}, s);
}
template <typename... Ts> constexpr auto gather_elems(ctll::list<mk::cm, Ts...>) noexcept {
	return gather_loop<true>(epack<>{}, ctll::list<Ts...>{});
}

template <typename E0, typename... Es> struct first_of {
	using type = E0;
};

// kwargs (or defaulted params) must come after all plain elements
template <typename... Es> constexpr bool kwargs_at_end() noexcept {
	bool seen = false;
	bool ok = true;
	(((is_kwarg<Es> ? (void)(seen = true) : (void)(ok = ok && !seen))), ...);
	return ok;
}

// --- displays and trailers -------------------------------------------

template <typename G> constexpr auto close_paren_impl(G) noexcept {
	return ctll::reject{};
}
template <typename... Es, bool Tr, typename... Ts>
constexpr auto close_paren_impl(gathered<epack<Es...>, Tr, ctll::list<mk::pmark, Ts...>>) noexcept {
	if constexpr (!(is_expr<Es> && ...)) {
		return ctll::reject{};
	} else if constexpr (sizeof...(Es) == 1 && !Tr) {
		return ctll::list<Es..., Ts...>{}; // grouped expression
	} else {
		return ctll::list<ast::tuple_expr<Es...>, Ts...>{};
	}
}

template <typename G> constexpr auto close_list_impl(G) noexcept {
	return ctll::reject{};
}
template <typename... Es, bool Tr, typename... Ts>
constexpr auto close_list_impl(gathered<epack<Es...>, Tr, ctll::list<mk::lmark, Ts...>>) noexcept {
	if constexpr (!(is_expr<Es> && ...)) {
		return ctll::reject{};
	} else {
		return ctll::list<ast::list_expr<Es...>, Ts...>{};
	}
}

template <typename G> constexpr auto close_brace_impl(G) noexcept {
	return ctll::reject{};
}
template <typename... Es, bool Tr, typename... Ts>
constexpr auto close_brace_impl(gathered<epack<Es...>, Tr, ctll::list<mk::brmark, Ts...>>) noexcept {
	if constexpr (sizeof...(Es) == 0) {
		return ctll::list<ast::dict_expr<>, Ts...>{}; // {} is a dict
	} else if constexpr ((is_dict_item<Es> && ...)) {
		return ctll::list<ast::dict_expr<Es...>, Ts...>{};
	} else if constexpr ((is_expr<Es> && ...)) {
		return ctll::list<ast::set_expr<Es...>, Ts...>{};
	} else {
		return ctll::reject{}; // mixed dict/set display
	}
}

template <typename G> constexpr auto close_call_impl(G) noexcept {
	return ctll::reject{};
}
template <typename... Es, bool Tr, typename C, typename... Ts>
constexpr auto close_call_impl(gathered<epack<Es...>, Tr, ctll::list<mk::cmark<C>, Ts...>>) noexcept {
	if constexpr (!((is_expr<Es> || is_kwarg<Es>) && ...)) {
		return ctll::reject{};
	} else if constexpr (!kwargs_at_end<Es...>()) {
		return ctll::reject{}; // positional argument after keyword argument
	} else {
		return ctll::list<ast::call_expr<C, Es...>, Ts...>{};
	}
}

template <typename E> struct as_param {
	using type = void;
};
template <typename N> struct as_param<ast::name<N>> {
	using type = ast::param<N, void>;
};
template <typename N, typename V> struct as_param<ast::kwarg<N, V>> {
	using type = ast::param<N, V>;
};

template <typename G> constexpr auto close_params_impl(G) noexcept {
	return ctll::reject{};
}
template <typename... Es, bool Tr, typename... Ts>
constexpr auto close_params_impl(gathered<epack<Es...>, Tr, ctll::list<mk::parm, Ts...>>) noexcept {
	if constexpr ((std::is_void_v<typename as_param<Es>::type> || ...)) {
		return ctll::reject{};
	} else if constexpr (!kwargs_at_end<Es...>()) {
		return ctll::reject{}; // non-default parameter after a default
	} else {
		return ctll::list<ast::param_pack<typename as_param<Es>::type...>, Ts...>{};
	}
}

// --- subscripts: index or optional-flanked slice ----------------------

template <typename... Is> struct ipack { };
template <typename Pack, typename Obj, typename Rest> struct subg { };

template <typename... Acc> constexpr auto gather_sub(ipack<Acc...>, ctll::list<>) noexcept {
	return ctll::reject{};
}
template <typename... Acc, typename Obj, typename... Ts>
constexpr auto gather_sub(ipack<Acc...>, ctll::list<mk::imark<Obj>, Ts...>) noexcept {
	return subg<ipack<Acc...>, Obj, ctll::list<Ts...>>{};
}
template <typename... Acc, typename H, typename... Ts>
constexpr auto gather_sub(ipack<Acc...>, ctll::list<H, Ts...>) noexcept {
	if constexpr (is_expr<H> || std::is_same_v<H, mk::sc>) {
		return gather_sub(ipack<H, Acc...>{}, ctll::list<Ts...>{});
	} else {
		return ctll::reject{};
	}
}

// walk the items in source order, assigning L : U : S slots
template <int NC, typename L, typename U, typename S> struct sstate { };

template <int NC, typename L, typename U, typename S>
constexpr auto sub_walk(sstate<NC, L, U, S> st, ipack<>) noexcept {
	return st;
}
template <int NC, typename L, typename U, typename S, typename I, typename... Is>
constexpr auto sub_walk(sstate<NC, L, U, S>, ipack<I, Is...>) noexcept {
	if constexpr (std::is_same_v<I, mk::sc>) {
		if constexpr (NC >= 2) {
			return ctll::reject{}; // a[i:j:k:l]
		} else {
			return sub_walk(sstate<NC + 1, L, U, S>{}, ipack<Is...>{});
		}
	} else if constexpr (NC == 0) {
		return sub_walk(sstate<NC, I, U, S>{}, ipack<Is...>{});
	} else if constexpr (NC == 1) {
		return sub_walk(sstate<NC, L, I, S>{}, ipack<Is...>{});
	} else {
		return sub_walk(sstate<NC, L, U, I>{}, ipack<Is...>{});
	}
}

template <typename Obj, typename Rest, typename W> struct sub_built {
	using type = ctll::reject;
};
template <typename Obj, typename... Ts, typename L, typename U, typename S>
struct sub_built<Obj, ctll::list<Ts...>, sstate<0, L, U, S>> {
	using type = std::conditional_t<std::is_void_v<L>, ctll::reject,
	                                ctll::list<ast::subscript_expr<Obj, L>, Ts...>>;
};
template <typename Obj, typename... Ts, int NC, typename L, typename U, typename S>
struct sub_built<Obj, ctll::list<Ts...>, sstate<NC, L, U, S>> {
	using type = ctll::list<ast::subscript_expr<Obj, ast::slice_expr<L, U, S>>, Ts...>;
};

template <typename G> constexpr auto close_sub_impl(G) noexcept {
	return ctll::reject{};
}
template <typename... Is, typename Obj, typename... Ts>
constexpr auto close_sub_impl(subg<ipack<Is...>, Obj, ctll::list<Ts...>>) noexcept {
	auto w = sub_walk(sstate<0, void, void, void>{}, ipack<Is...>{});
	if constexpr (std::is_same_v<decltype(w), ctll::reject>) {
		return ctll::reject{};
	} else {
		return typename sub_built<Obj, ctll::list<Ts...>, decltype(w)>::type{};
	}
}

// --- operator pushes ---------------------------------------------------

template <typename Op> constexpr auto push_bin_checked(ctll::list<>) noexcept {
	return ctll::reject{};
}
template <typename Op, typename E, typename... Ts>
constexpr auto push_bin_checked(ctll::list<E, Ts...>) noexcept {
	if constexpr (is_expr<E>) {
		return ctll::list<mk::bop<Op>, E, Ts...>{};
	} else {
		return ctll::reject{};
	}
}
template <typename Op, typename... Ts> constexpr auto push_bin(ctll::list<Ts...> s) noexcept {
	return push_bin_checked<Op>(fold_ops<prec_of<Op>, right_assoc_of<Op>>(s));
}

template <typename Op, typename... Ts> constexpr auto push_un(ctll::list<Ts...>) noexcept {
	return ctll::list<mk::uop<Op>, Ts...>{};
}

template <typename... Ts> constexpr auto not_impl(ctll::list<Ts...> s) noexcept {
	return push_un<ast::op_not>(s);
}
template <typename... Ts> constexpr auto not_impl(ctll::list<mk::bop<ast::op_is>, Ts...>) noexcept {
	return ctll::list<mk::bop<ast::op_is_not>, Ts...>{}; // "is not"
}

template <typename... Ts> constexpr auto tern_if_impl(ctll::list<Ts...>) noexcept {
	return ctll::reject{};
}
template <typename E, typename... Ts> constexpr auto tern_if_impl(ctll::list<E, Ts...>) noexcept {
	if constexpr (is_expr<E>) {
		return ctll::list<mk::t_if, E, Ts...>{};
	} else {
		return ctll::reject{};
	}
}

template <typename... Ts> constexpr auto tern_else_impl(ctll::list<Ts...>) noexcept {
	return ctll::reject{};
}
template <typename C, typename Th, typename... Ts>
constexpr auto tern_else_impl(ctll::list<C, mk::t_if, Th, Ts...>) noexcept {
	if constexpr (is_expr<C> && is_expr<Th>) {
		return ctll::list<mk::t_pair<Th, C>, Ts...>{};
	} else {
		return ctll::reject{};
	}
}

// --- separators, kwargs, colons ---------------------------------------

template <typename... Ts> constexpr auto sep_impl(ctll::list<Ts...>) noexcept {
	return ctll::reject{};
}
template <typename E, typename... Ts> constexpr auto sep_impl(ctll::list<E, Ts...>) noexcept {
	if constexpr (is_element<E>) {
		return ctll::list<mk::cm, E, Ts...>{};
	} else {
		return ctll::reject{};
	}
}

// is this = a kwarg/default position? (inside call args or params)
constexpr bool kw_ctx(ctll::list<>) noexcept {
	return false;
}
template <typename C, typename... Ts> constexpr bool kw_ctx(ctll::list<mk::cmark<C>, Ts...>) noexcept {
	return true;
}
template <typename... Ts> constexpr bool kw_ctx(ctll::list<mk::parm, Ts...>) noexcept {
	return true;
}
template <typename H, typename... Ts> constexpr bool kw_ctx(ctll::list<H, Ts...>) noexcept {
	if constexpr (is_element<H> || std::is_same_v<H, mk::cm>) {
		return kw_ctx(ctll::list<Ts...>{});
	} else {
		return false;
	}
}

template <typename... Ts> constexpr auto dict_colon_impl(ctll::list<Ts...>) noexcept {
	return ctll::reject{};
}
template <typename K, typename... Ts> constexpr auto dict_colon_impl(ctll::list<K, Ts...>) noexcept {
	if constexpr (is_expr<K>) {
		return ctll::list<mk::dmark, K, Ts...>{};
	} else {
		return ctll::reject{};
	}
}

// --- assignment --------------------------------------------------------

template <typename L> struct head_rejects_assign : std::false_type { };
template <typename... Ts> struct head_rejects_assign<ctll::list<mk::retm, Ts...>> : std::true_type { };
template <typename Op, typename T, typename... Ts>
struct head_rejects_assign<ctll::list<mk::gmark<Op, T>, Ts...>> : std::true_type { };

template <typename G> constexpr auto assign_impl(G) noexcept {
	return ctll::reject{};
}
template <typename... Es, bool Tr, typename... Ts>
constexpr auto assign_impl(gathered<epack<Es...>, Tr, ctll::list<Ts...>>) noexcept {
	if constexpr (sizeof...(Es) == 0 || !(is_target<Es> && ...) ||
	              head_rejects_assign<ctll::list<Ts...>>::value) {
		return ctll::reject{};
	} else if constexpr (sizeof...(Es) == 1 && !Tr) {
		return ctll::list<mk::amark<typename first_of<Es...>::type>, Ts...>{};
	} else {
		return ctll::list<mk::amark<ast::tuple_expr<Es...>>, Ts...>{};
	}
}

template <typename L> struct head_rejects_aug : std::false_type { };
template <typename... Ts> struct head_rejects_aug<ctll::list<mk::retm, Ts...>> : std::true_type { };
template <typename Op, typename T, typename... Ts>
struct head_rejects_aug<ctll::list<mk::gmark<Op, T>, Ts...>> : std::true_type { };
template <typename T, typename... Ts>
struct head_rejects_aug<ctll::list<mk::amark<T>, Ts...>> : std::true_type { };

template <typename Op, typename G> constexpr auto aug_impl(G) noexcept {
	return ctll::reject{};
}
template <typename Op, typename E, typename... Ts>
constexpr auto aug_impl(gathered<epack<E>, false, ctll::list<Ts...>>) noexcept {
	if constexpr (!is_simple_target<E> || head_rejects_aug<ctll::list<Ts...>>::value) {
		return ctll::reject{};
	} else {
		return ctll::list<mk::gmark<Op, E>, Ts...>{};
	}
}

// --- ending a simple statement -----------------------------------------

template <typename... Tg> struct tpack { };

template <typename V, typename... Tg, typename... Ts>
constexpr auto collect_targets(tpack<Tg...>, ctll::list<Ts...>) noexcept {
	return ctll::list<ast::assign_stmt<V, Tg...>, Ts...>{};
}
template <typename V, typename... Tg, typename T, typename... Ts>
constexpr auto collect_targets(tpack<Tg...>, ctll::list<mk::amark<T>, Ts...>) noexcept {
	return collect_targets<V>(tpack<T, Tg...>{}, ctll::list<Ts...>{});
}

template <typename L> struct stmt_boundary : std::false_type { };
template <> struct stmt_boundary<ctll::list<>> : std::true_type { };
template <typename H, typename... Ts> struct stmt_boundary<ctll::list<H, Ts...>> {
	static constexpr bool value = is_stmt<H> || std::is_same_v<H, mk::sm>;
};

template <typename V, typename... Rs> constexpr auto end_fin(ctll::list<Rs...>) noexcept {
	if constexpr (std::is_void_v<V> || !stmt_boundary<ctll::list<Rs...>>::value) {
		return ctll::reject{};
	} else {
		return ctll::list<ast::expr_stmt<V>, Rs...>{};
	}
}
template <typename V, typename... Ts> constexpr auto end_fin(ctll::list<mk::retm, Ts...>) noexcept {
	return ctll::list<ast::return_stmt<V>, Ts...>{};
}
template <typename V, typename Op, typename T, typename... Ts>
constexpr auto end_fin(ctll::list<mk::gmark<Op, T>, Ts...>) noexcept {
	if constexpr (std::is_void_v<V>) {
		return ctll::reject{};
	} else {
		return ctll::list<ast::aug_stmt<Op, T, V>, Ts...>{};
	}
}
template <typename V, typename T, typename... Ts>
constexpr auto end_fin(ctll::list<mk::amark<T>, Ts...> s) noexcept {
	if constexpr (std::is_void_v<V>) {
		return ctll::reject{};
	} else {
		return collect_targets<V>(tpack<>{}, s);
	}
}

template <typename G> constexpr auto end_stmt_impl(G) noexcept {
	return ctll::reject{};
}
template <typename... Es, bool Tr, typename... Rs>
constexpr auto end_stmt_impl(gathered<epack<Es...>, Tr, ctll::list<Rs...>>) noexcept {
	if constexpr (!(is_expr<Es> && ...)) {
		return ctll::reject{};
	} else if constexpr (sizeof...(Es) == 0) {
		return end_fin<void>(ctll::list<Rs...>{});
	} else if constexpr (sizeof...(Es) == 1 && !Tr) {
		return end_fin<typename first_of<Es...>::type>(ctll::list<Rs...>{});
	} else {
		return end_fin<ast::tuple_expr<Es...>>(ctll::list<Rs...>{});
	}
}

template <typename L> struct head_stmt : std::false_type { };
template <typename H, typename... Ts> struct head_stmt<ctll::list<H, Ts...>> {
	static constexpr bool value = is_stmt<H>;
};

// --- for-loop targets ---------------------------------------------------

template <typename G> constexpr auto for_in_impl2(G) noexcept {
	return ctll::reject{};
}
template <typename... Es, bool Tr, typename... Ts>
constexpr auto for_in_impl2(gathered<epack<Es...>, Tr, ctll::list<mk::form, Ts...>>) noexcept {
	if constexpr (sizeof...(Es) == 0 || !(is_name_node<Es> && ...)) {
		return ctll::reject{};
	} else if constexpr (sizeof...(Es) == 1 && !Tr) {
		return ctll::list<mk::inm<typename first_of<Es...>::type>, Ts...>{};
	} else {
		return ctll::list<mk::inm<ast::tuple_expr<Es...>>, Ts...>{};
	}
}

// --- suites and compound statements ------------------------------------

template <typename... Ss> struct spack { };
template <typename Pack, typename Rest> struct stg { };

template <typename... Acc> constexpr auto gather_stmts(spack<Acc...>, ctll::list<>) noexcept {
	return ctll::reject{};
}
template <typename... Acc, typename H, typename... Ts>
constexpr auto gather_stmts(spack<Acc...>, ctll::list<H, Ts...>) noexcept {
	if constexpr (std::is_same_v<H, mk::sm>) {
		return stg<spack<Acc...>, ctll::list<Ts...>>{};
	} else if constexpr (is_stmt<H>) {
		return gather_stmts(spack<H, Acc...>{}, ctll::list<Ts...>{});
	} else {
		return ctll::reject{};
	}
}

// attach a closed suite to the header below it
template <typename S, typename L> struct hdr_folded {
	using type = ctll::reject;
};
template <typename S, typename T, typename... Rs> struct hdr_folded<S, ctll::list<T, mk::ifm, Rs...>> {
	using type = std::conditional_t<is_expr<T>,
	                                ctll::list<ast::if_stmt<T, S, ast::clause_pack<>, void>, Rs...>,
	                                ctll::reject>;
};
template <typename S, typename T, typename IT, typename IB, typename... Cs, typename... Rs>
struct hdr_folded<S, ctll::list<T, mk::elifm, ast::if_stmt<IT, IB, ast::clause_pack<Cs...>, void>, Rs...>> {
	using type = std::conditional_t<
		is_expr<T>,
		ctll::list<ast::if_stmt<IT, IB, ast::clause_pack<Cs..., ast::elif_clause<T, S>>, void>, Rs...>,
		ctll::reject>;
};
template <typename S, typename IT, typename IB, typename P, typename... Rs>
struct hdr_folded<S, ctll::list<mk::elsem, ast::if_stmt<IT, IB, P, void>, Rs...>> {
	using type = ctll::list<ast::if_stmt<IT, IB, P, S>, Rs...>;
};
template <typename S, typename T, typename... Rs> struct hdr_folded<S, ctll::list<T, mk::whilem, Rs...>> {
	using type = std::conditional_t<is_expr<T>,
	                                ctll::list<ast::while_stmt<T, S, void>, Rs...>,
	                                ctll::reject>;
};
template <typename S, typename WT, typename WB, typename... Rs>
struct hdr_folded<S, ctll::list<mk::elsem, ast::while_stmt<WT, WB, void>, Rs...>> {
	using type = ctll::list<ast::while_stmt<WT, WB, S>, Rs...>;
};
template <typename S, typename I, typename Tg, typename... Rs>
struct hdr_folded<S, ctll::list<I, mk::inm<Tg>, Rs...>> {
	using type = std::conditional_t<is_expr<I>,
	                                ctll::list<ast::for_stmt<Tg, I, S, void>, Rs...>,
	                                ctll::reject>;
};
template <typename S, typename Tg, typename I, typename B, typename... Rs>
struct hdr_folded<S, ctll::list<mk::elsem, ast::for_stmt<Tg, I, B, void>, Rs...>> {
	using type = ctll::list<ast::for_stmt<Tg, I, B, S>, Rs...>;
};
template <typename S, typename... Ps, typename N, typename... Rs>
struct hdr_folded<S, ctll::list<ast::param_pack<Ps...>, ast::name<N>, mk::defm, Rs...>> {
	using type = ctll::list<ast::def_stmt<N, ast::param_pack<Ps...>, S>, Rs...>;
};

template <bool Inline, typename G> constexpr auto close_suite_impl(G) noexcept {
	return ctll::reject{};
}
template <bool Inline, typename... Ss, typename... Rs>
constexpr auto close_suite_impl(stg<spack<Ss...>, ctll::list<Rs...>>) noexcept {
	if constexpr (sizeof...(Ss) == 0) {
		return ctll::reject{};
	} else if constexpr (Inline && (sizeof...(Ss) != 1 || !(is_simple_stmt<Ss> && ...))) {
		return ctll::reject{}; // a compound statement needs its own line
	} else {
		return typename hdr_folded<ast::suite<Ss...>, ctll::list<Rs...>>::type{};
	}
}

// --- module -------------------------------------------------------------

template <typename... Acc> constexpr auto mod_gather(spack<Acc...>, ctll::list<>) noexcept {
	return ctll::list<ast::module<Acc...>>{};
}
template <typename... Acc, typename H, typename... Ts>
constexpr auto mod_gather(spack<Acc...>, ctll::list<H, Ts...>) noexcept {
	if constexpr (is_stmt<H>) {
		return mod_gather(spack<H, Acc...>{}, ctll::list<Ts...>{});
	} else {
		return ctll::reject{};
	}
}

// --- the per-action dispatch: act(Action, Term, Stack) -> Stack/reject --

// unknown action or wrong stack shape: reject (soft parse failure)
template <typename A, typename Tm, typename... Ts>
constexpr auto act(A, Tm, ctll::list<Ts...>) noexcept {
	return ctll::reject{};
}

// text accumulation
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::begin_text, Tm, ctll::list<Ts...>) noexcept {
	return ctll::list<text<>, Ts...>{};
}
template <auto V, auto... Cs, typename... Ts>
constexpr auto act(python_grammar::push_char, ctll::term<V>, ctll::list<text<Cs...>, Ts...>) noexcept {
	return ctll::list<text<Cs..., V>, Ts...>{};
}
template <typename Tm, auto... Cs, typename... Ts>
constexpr auto act(python_grammar::push_sq, Tm, ctll::list<text<Cs...>, Ts...>) noexcept {
	return ctll::list<text<Cs..., '\''>, Ts...>{};
}
template <typename Tm, auto... Cs, typename... Ts>
constexpr auto act(python_grammar::push_dq, Tm, ctll::list<text<Cs...>, Ts...>) noexcept {
	return ctll::list<text<Cs..., '"'>, Ts...>{};
}

// names, numbers, attributes
template <typename Tm, auto... Cs, typename... Ts>
constexpr auto act(python_grammar::mk_name, Tm, ctll::list<text<Cs...>, Ts...>) noexcept {
	if constexpr (is_reserved(text<Cs...>::view())) {
		return ctll::reject{};
	} else {
		return ctll::list<ast::name<text<Cs...>>, Ts...>{};
	}
}
template <typename Tm, auto... Cs, typename... Ts>
constexpr auto act(python_grammar::mk_int, Tm, ctll::list<text<Cs...>, Ts...>) noexcept {
	return ctll::list<ast::int_lit<text<Cs...>>, Ts...>{};
}
template <typename Tm, auto... Cs, typename... Ts>
constexpr auto act(python_grammar::mk_float, Tm, ctll::list<text<Cs...>, Ts...>) noexcept {
	return ctll::list<ast::float_lit<text<Cs...>>, Ts...>{};
}
template <typename Tm, auto... Cs, typename E, typename... Ts>
constexpr auto act(python_grammar::mk_attr, Tm, ctll::list<text<Cs...>, E, Ts...>) noexcept {
	if constexpr (!is_expr<E> || is_reserved(text<Cs...>::view())) {
		return ctll::reject{};
	} else {
		return ctll::list<ast::attribute_expr<E, text<Cs...>>, Ts...>{};
	}
}

// string literals (bodies accumulate verbatim; f-strings differ only
// in the marker the fold sees)
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::begin_str, Tm, ctll::list<Ts...>) noexcept {
	return ctll::list<text<>, mk::smark, Ts...>{};
}
template <typename Tm, auto... Cs, typename... Ts>
constexpr auto act(python_grammar::begin_fstr, Tm, ctll::list<text<Cs...>, Ts...>) noexcept {
	return ctll::list<text<>, mk::fmark, Ts...>{}; // drops the accumulated "f"
}
template <typename Tm, auto... Cs, typename... Ts>
constexpr auto act(python_grammar::end_str, Tm, ctll::list<text<Cs...>, mk::smark, Ts...>) noexcept {
	return ctll::list<ast::str_lit<text<Cs...>>, Ts...>{};
}
template <typename Tm, auto... Cs, typename... Ts>
constexpr auto act(python_grammar::end_str, Tm, ctll::list<text<Cs...>, mk::fmark, Ts...>) noexcept {
	return ctll::list<ast::fstr_lit<text<Cs...>>, Ts...>{};
}

// keyword literals (the chain accumulated their spelling; drop it)
template <typename Tm, auto... Cs, typename... Ts>
constexpr auto act(python_grammar::mk_none, Tm, ctll::list<text<Cs...>, Ts...>) noexcept {
	return ctll::list<ast::none_lit, Ts...>{};
}
template <typename Tm, auto... Cs, typename... Ts>
constexpr auto act(python_grammar::mk_true, Tm, ctll::list<text<Cs...>, Ts...>) noexcept {
	return ctll::list<ast::true_lit, Ts...>{};
}
template <typename Tm, auto... Cs, typename... Ts>
constexpr auto act(python_grammar::mk_false, Tm, ctll::list<text<Cs...>, Ts...>) noexcept {
	return ctll::list<ast::false_lit, Ts...>{};
}

// binary operators
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::op_or, Tm, ctll::list<Ts...> s) noexcept {
	return push_bin<ast::op_or>(s);
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::op_and, Tm, ctll::list<Ts...> s) noexcept {
	return push_bin<ast::op_and>(s);
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::op_in, Tm, ctll::list<Ts...> s) noexcept {
	return push_bin<ast::op_in>(s);
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::op_nin, Tm, ctll::list<Ts...> s) noexcept {
	return push_bin<ast::op_not_in>(s);
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::op_is, Tm, ctll::list<Ts...> s) noexcept {
	return push_bin<ast::op_is>(s);
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::op_lt, Tm, ctll::list<Ts...> s) noexcept {
	return push_bin<ast::op_lt>(s);
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::op_le, Tm, ctll::list<Ts...> s) noexcept {
	return push_bin<ast::op_le>(s);
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::op_gt, Tm, ctll::list<Ts...> s) noexcept {
	return push_bin<ast::op_gt>(s);
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::op_ge, Tm, ctll::list<Ts...> s) noexcept {
	return push_bin<ast::op_ge>(s);
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::op_eq, Tm, ctll::list<Ts...> s) noexcept {
	return push_bin<ast::op_eq>(s);
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::op_ne, Tm, ctll::list<Ts...> s) noexcept {
	return push_bin<ast::op_ne>(s);
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::op_bor, Tm, ctll::list<Ts...> s) noexcept {
	return push_bin<ast::op_bor>(s);
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::op_bxor, Tm, ctll::list<Ts...> s) noexcept {
	return push_bin<ast::op_bxor>(s);
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::op_band, Tm, ctll::list<Ts...> s) noexcept {
	return push_bin<ast::op_band>(s);
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::op_shl, Tm, ctll::list<Ts...> s) noexcept {
	return push_bin<ast::op_shl>(s);
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::op_shr, Tm, ctll::list<Ts...> s) noexcept {
	return push_bin<ast::op_shr>(s);
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::op_add, Tm, ctll::list<Ts...> s) noexcept {
	return push_bin<ast::op_add>(s);
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::op_sub, Tm, ctll::list<Ts...> s) noexcept {
	return push_bin<ast::op_sub>(s);
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::op_mul, Tm, ctll::list<Ts...> s) noexcept {
	return push_bin<ast::op_mul>(s);
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::op_div, Tm, ctll::list<Ts...> s) noexcept {
	return push_bin<ast::op_div>(s);
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::op_fdiv, Tm, ctll::list<Ts...> s) noexcept {
	return push_bin<ast::op_floordiv>(s);
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::op_mod, Tm, ctll::list<Ts...> s) noexcept {
	return push_bin<ast::op_mod>(s);
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::op_pow, Tm, ctll::list<Ts...> s) noexcept {
	return push_bin<ast::op_pow>(s);
}

// unary operators; "not" also upgrades a pending "is" to "is not"
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::op_neg, Tm, ctll::list<Ts...> s) noexcept {
	return push_un<ast::op_neg>(s);
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::op_pos, Tm, ctll::list<Ts...> s) noexcept {
	return push_un<ast::op_pos>(s);
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::op_inv, Tm, ctll::list<Ts...> s) noexcept {
	return push_un<ast::op_invert>(s);
}
template <typename Tm, auto... Cs, typename... Ts>
constexpr auto act(python_grammar::op_not, Tm, ctll::list<text<Cs...>, Ts...>) noexcept {
	return not_impl(ctll::list<Ts...>{});
}

// ternary
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::tern_if, Tm, ctll::list<Ts...> s) noexcept {
	return tern_if_impl(fold_ops<2, false>(s));
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::tern_else, Tm, ctll::list<Ts...> s) noexcept {
	return tern_else_impl(fold_ops<2, false>(s));
}

// displays and trailers
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::open_paren, Tm, ctll::list<Ts...>) noexcept {
	return ctll::list<mk::pmark, Ts...>{};
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::close_paren, Tm, ctll::list<Ts...> s) noexcept {
	return close_paren_impl(gather_elems(fold_ready(s)));
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::open_list, Tm, ctll::list<Ts...>) noexcept {
	return ctll::list<mk::lmark, Ts...>{};
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::close_list, Tm, ctll::list<Ts...> s) noexcept {
	return close_list_impl(gather_elems(fold_ready(s)));
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::open_brace, Tm, ctll::list<Ts...>) noexcept {
	return ctll::list<mk::brmark, Ts...>{};
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::close_brace, Tm, ctll::list<Ts...> s) noexcept {
	return close_brace_impl(gather_elems(fold_ready(s)));
}
template <typename Tm, typename E, typename... Ts>
constexpr auto act(python_grammar::open_call, Tm, ctll::list<E, Ts...>) noexcept {
	if constexpr (is_expr<E>) {
		return ctll::list<mk::cmark<E>, Ts...>{};
	} else {
		return ctll::reject{};
	}
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::close_call, Tm, ctll::list<Ts...> s) noexcept {
	return close_call_impl(gather_elems(fold_ready(s)));
}
template <typename Tm, typename E, typename... Ts>
constexpr auto act(python_grammar::open_sub, Tm, ctll::list<E, Ts...>) noexcept {
	if constexpr (is_expr<E>) {
		return ctll::list<mk::imark<E>, Ts...>{};
	} else {
		return ctll::reject{};
	}
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::close_sub, Tm, ctll::list<Ts...> s) noexcept {
	return close_sub_impl(gather_sub(ipack<>{}, fold_ready(s)));
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::slice_colon, Tm, ctll::list<Ts...> s) noexcept {
	return prepended<mk::sc>(fold_ready(s));
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::sep_comma, Tm, ctll::list<Ts...> s) noexcept {
	return sep_impl(fold_ready(s));
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::dict_colon, Tm, ctll::list<Ts...> s) noexcept {
	return dict_colon_impl(fold_all(s));
}
template <typename Tm, typename N, typename... Ts>
constexpr auto act(python_grammar::mk_kweq, Tm, ctll::list<ast::name<N>, Ts...>) noexcept {
	if constexpr (kw_ctx(ctll::list<Ts...>{})) {
		return ctll::list<mk::kwmark<N>, Ts...>{};
	} else {
		return ctll::reject{};
	}
}

// assignment
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::mk_assign, Tm, ctll::list<Ts...> s) noexcept {
	return assign_impl(gather_elems(fold_ready(s)));
}
template <typename Tm, typename Op, typename... Ts>
constexpr auto act(python_grammar::mk_aug, Tm, ctll::list<mk::bop<Op>, Ts...>) noexcept {
	return aug_impl<Op>(gather_elems(fold_ready(ctll::list<Ts...>{})));
}

// simple statements
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::end_stmt, Tm, ctll::list<Ts...> s) noexcept {
	if constexpr (head_stmt<ctll::list<Ts...>>::value) {
		return s; // break/continue/pass already folded themselves
	} else {
		return end_stmt_impl(gather_elems(fold_ready(s)));
	}
}
template <typename Tm, auto... Cs, typename... Ts>
constexpr auto act(python_grammar::kw_break, Tm, ctll::list<text<Cs...>, Ts...>) noexcept {
	return ctll::list<ast::break_stmt, Ts...>{};
}
template <typename Tm, auto... Cs, typename... Ts>
constexpr auto act(python_grammar::kw_continue, Tm, ctll::list<text<Cs...>, Ts...>) noexcept {
	return ctll::list<ast::continue_stmt, Ts...>{};
}
template <typename Tm, auto... Cs, typename... Ts>
constexpr auto act(python_grammar::kw_pass, Tm, ctll::list<text<Cs...>, Ts...>) noexcept {
	return ctll::list<ast::pass_stmt, Ts...>{};
}
template <typename Tm, auto... Cs, typename... Ts>
constexpr auto act(python_grammar::kw_return, Tm, ctll::list<text<Cs...>, Ts...>) noexcept {
	return ctll::list<mk::retm, Ts...>{};
}

// compound statement headers
template <typename Tm, auto... Cs, typename... Ts>
constexpr auto act(python_grammar::kw_if, Tm, ctll::list<text<Cs...>, Ts...>) noexcept {
	return ctll::list<mk::ifm, Ts...>{};
}
template <typename Tm, auto... Cs, typename T, typename B, typename P, typename... Ts>
constexpr auto act(python_grammar::kw_elif, Tm,
                   ctll::list<text<Cs...>, ast::if_stmt<T, B, P, void>, Ts...>) noexcept {
	return ctll::list<mk::elifm, ast::if_stmt<T, B, P, void>, Ts...>{};
}
template <typename Tm, auto... Cs, typename T, typename B, typename P, typename... Ts>
constexpr auto act(python_grammar::kw_else, Tm,
                   ctll::list<text<Cs...>, ast::if_stmt<T, B, P, void>, Ts...>) noexcept {
	return ctll::list<mk::elsem, ast::if_stmt<T, B, P, void>, Ts...>{};
}
template <typename Tm, auto... Cs, typename T, typename B, typename... Ts>
constexpr auto act(python_grammar::kw_else, Tm,
                   ctll::list<text<Cs...>, ast::while_stmt<T, B, void>, Ts...>) noexcept {
	return ctll::list<mk::elsem, ast::while_stmt<T, B, void>, Ts...>{};
}
template <typename Tm, auto... Cs, typename Tg, typename I, typename B, typename... Ts>
constexpr auto act(python_grammar::kw_else, Tm,
                   ctll::list<text<Cs...>, ast::for_stmt<Tg, I, B, void>, Ts...>) noexcept {
	return ctll::list<mk::elsem, ast::for_stmt<Tg, I, B, void>, Ts...>{};
}
template <typename Tm, auto... Cs, typename... Ts>
constexpr auto act(python_grammar::kw_while, Tm, ctll::list<text<Cs...>, Ts...>) noexcept {
	return ctll::list<mk::whilem, Ts...>{};
}
template <typename Tm, auto... Cs, typename... Ts>
constexpr auto act(python_grammar::kw_for, Tm, ctll::list<text<Cs...>, Ts...>) noexcept {
	return ctll::list<mk::form, Ts...>{};
}
template <typename Tm, auto... Cs, typename... Ts>
constexpr auto act(python_grammar::for_in, Tm, ctll::list<text<Cs...>, Ts...>) noexcept {
	return for_in_impl2(gather_elems(ctll::list<Ts...>{}));
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::for_in, Tm, ctll::list<Ts...> s) noexcept {
	return for_in_impl2(gather_elems(s));
}
template <typename Tm, auto... Cs, typename... Ts>
constexpr auto act(python_grammar::kw_def, Tm, ctll::list<text<Cs...>, Ts...>) noexcept {
	return ctll::list<mk::defm, Ts...>{};
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::open_params, Tm, ctll::list<Ts...>) noexcept {
	return ctll::list<mk::parm, Ts...>{};
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::close_params, Tm, ctll::list<Ts...> s) noexcept {
	return close_params_impl(gather_elems(fold_ready(s)));
}

// suites
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::begin_suite, Tm, ctll::list<Ts...> s) noexcept {
	return prepended<mk::sm>(fold_ready(s));
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::close_block, Tm, ctll::list<Ts...> s) noexcept {
	return close_suite_impl<false>(gather_stmts(spack<>{}, s));
}
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::close_inline, Tm, ctll::list<Ts...> s) noexcept {
	return close_suite_impl<true>(gather_stmts(spack<>{}, s));
}

// module
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::end_module, Tm, ctll::list<Ts...> s) noexcept {
	return mod_gather(spack<>{}, s);
}

// explicit boundary rejection ("x ory")
template <typename Tm, typename... Ts>
constexpr auto act(python_grammar::kw_reject, Tm, ctll::list<Ts...>) noexcept {
	return ctll::reject{};
}

} // namespace detail

// the CTLL action selector: unwrap the context, run the action, wrap
// the result (or propagate the rejection)
CTPY_EXPORT struct python_actions {
	template <typename Result> static constexpr auto wrap(Result) noexcept {
		if constexpr (std::is_same_v<Result, ctll::reject>) {
			return ctll::reject{};
		} else {
			return context<Result>{};
		}
	}

	template <typename Action, typename Term, typename... Ts>
	static constexpr auto apply(Action, Term, context<ctll::list<Ts...>>) noexcept {
		return wrap(detail::act(Action{}, Term{}, ctll::list<Ts...>{}));
	}
};

} // namespace ctpy

#endif
