#ifndef CTPY__EXEC__HPP
#define CTPY__EXEC__HPP

#include "version.hpp"
#include "text.hpp"
#include "ast.hpp"
#include "object.hpp"
#include "parse.hpp"
#include "eval.hpp"
#include "builtins.hpp"

#ifndef CTPY_IN_A_MODULE
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#endif

// Statement execution: exec<Stmt>(state) -> Flow, the value-level
// tree-walk over compound statements. Control transfers are FLOW
// SIGNALS threaded through return values - a break/continue unwinds
// suites until the innermost loop absorbs it, a return until the
// calling thunk absorbs it. Python exceptions stay on the soft channel
// (state.raised + PyError, NEVER C++ exceptions): a raised state makes
// every subsequent statement a no-op and the leftover Flow::next
// carries the walk out.
//
// Python semantics implemented here, deliberately:
//   - chained assignment `a = b = v` evaluates v ONCE, then assigns
//     the targets left to right; tuple targets unpack any iterable
//     (tuple/list/str/range) with CPython's ValueError messages;
//   - `while`/`for` else-suites run only when the loop was NOT left
//     by break;
//   - for-loops iterate range objects lazily (no materialization), str
//     per character, dicts over their KEYS in insertion order;
//   - break/continue/return that escape to module level (or a function
//     body, for break/continue) are the SyntaxErrors CPython reports
//     (soft, since the grammar accepts them anywhere);
//   - def is a statement: it executes at flow time, evaluates its
//     defaults ONCE right then (Python def-time semantics), and binds
//     a function OBJECT; a call pushes one locals frame and pops it on
//     any exit; name resolution is own-locals-then-globals, so nested
//     defs see globals + their own locals only (NO closures in v0.1);
//     recursion is guarded by the soft RecursionError at depth 100.
//
// The public entry points over this walk - ctpy::run<Src>(args...),
// ctpy::eval<Src>(), ctpy::module<Src>.call<Fn>(...) - live in
// result.hpp, which right-sizes the dead arena into per-Src static
// storage and hands out the uniform ctpy::value views (views.hpp).

namespace ctpy {

// how a statement finished: fell through, or is transferring control
CTPY_EXPORT enum class Flow : unsigned char {
	next,      // fall through to the next statement
	break_,    // unwinding to the innermost loop
	continue_, // unwinding to the innermost loop's next iteration
	return_,   // unwinding to the innermost call (value in State::retval)
};

namespace detail {

template <typename Stmt> struct executor {
	static_assert(sizeof(Stmt) == 0,
		"ctpy: this statement kind is not executable yet (later milestone)");
};

template <typename Stmt, typename St> constexpr Flow exec_node(St & st) {
	if (st.raised) {
		return Flow::next; // exception in flight: every statement is a no-op
	}
	return executor<Stmt>::run(st);
}

// --- assignment targets ------------------------------------------------------

// no specialization = the target kind is not assignable (attribute
// stores are out of the v0.1 subset - no user objects to store into).
// The iteration helpers (iterable_kind/iter_len/iter_get) live in
// eval.hpp since M6 - indexing shares them.
template <typename Target> struct assign_target {
	static_assert(sizeof(Target) == 0,
		"ctpy: assignment to this target kind is not in the v0.1 subset");
};

template <typename Text> struct assign_target<ast::name<Text>> {
	template <typename St> static constexpr void run(St & st, std::uint32_t value) {
		st.bind(Text::view(), value);
	}
};

// a[i] = value / d[k] = value (the value is already evaluated - the
// assign statement evaluates its right side FIRST, then each target's
// object and index expressions, per CPython)
template <typename Obj, typename Index> struct assign_target<ast::subscript_expr<Obj, Index>> {
	template <typename St> static constexpr void run(St & st, std::uint32_t value) {
		const std::uint32_t object = eval_node<Obj, St>(st);
		if (st.raised) {
			return;
		}
		const std::uint32_t key = eval_node<Index, St>(st);
		if (st.raised) {
			return;
		}
		subscript_store(st, object, key, value);
	}
};

// slice stores are out of the v0.1 subset (soft error, documented)
template <typename Obj, typename L, typename U, typename S>
struct assign_target<ast::subscript_expr<Obj, ast::slice_expr<L, U, S>>> {
	template <typename St> static constexpr void run(St & st, std::uint32_t) {
		st.raise_error(ex_kind::TypeError, "ctpy v0.1: slice assignment is not supported");
	}
};

template <typename... Es> struct unpack_into;
template <> struct unpack_into<> {
	template <typename St> static constexpr void run(St &, const Object &, long long) noexcept { }
};
template <typename E, typename... Rest> struct unpack_into<E, Rest...> {
	template <typename St> static constexpr void run(St & st, const Object & source, long long at) {
		const std::uint32_t element = iter_get(st, source, at);
		assign_target<E>::run(st, element);
		if (st.raised) {
			return;
		}
		unpack_into<Rest...>::run(st, source, at + 1);
	}
};

// a, b = ... unpacks any iterable, arity-checked the CPython way
template <typename... Es> struct assign_target<ast::tuple_expr<Es...>> {
	template <typename St> static constexpr void run(St & st, std::uint32_t value) {
		const Object source = st.a.objs[value]; // copy: unpacking allocates
		if (!iterable_kind(source.kind)) {
			st.raise_error(ex_kind::TypeError,
				{"cannot unpack non-iterable ", type_name(source.kind), " object"});
			return;
		}
		constexpr long long want = static_cast<long long>(sizeof...(Es));
		const long long have = iter_len(st, source);
		if (have < want) {
			const auto wanted = dec(want);
			const auto got = dec(have);
			st.raise_error(ex_kind::ValueError,
				{"not enough values to unpack (expected ", wanted.view(), ", got ", got.view(), ")"});
			return;
		}
		if (have > want) {
			const auto wanted = dec(want);
			st.raise_error(ex_kind::ValueError,
				{"too many values to unpack (expected ", wanted.view(), ")"});
			return;
		}
		unpack_into<Es...>::run(st, source, 0);
	}
};

// --- simple statements ----------------------------------------------------------

template <typename E> struct executor<ast::expr_stmt<E>> {
	template <typename St> static constexpr Flow run(St & st) {
		(void)eval_node<E, St>(st);
		return Flow::next;
	}
};

template <> struct executor<ast::pass_stmt> {
	template <typename St> static constexpr Flow run(St &) noexcept {
		return Flow::next;
	}
};
template <> struct executor<ast::break_stmt> {
	template <typename St> static constexpr Flow run(St &) noexcept {
		return Flow::break_;
	}
};
template <> struct executor<ast::continue_stmt> {
	template <typename St> static constexpr Flow run(St &) noexcept {
		return Flow::continue_;
	}
};

// return: stash the value (None for a bare `return`) in the state's
// return-value channel and signal Flow::return_. The channel cannot be
// clobbered by a nested call: the innermost thunk reads it the moment
// the flow reaches it, before any other expression can run.
template <typename E> struct executor<ast::return_stmt<E>> {
	template <typename St> static constexpr Flow run(St & st) {
		if constexpr (std::is_void_v<E>) {
			st.retval = st.none();
		} else {
			st.retval = eval_node<E, St>(st);
			if (st.raised) {
				return Flow::next;
			}
		}
		return Flow::return_;
	}
};

template <typename V, typename... Targets> struct executor<ast::assign_stmt<V, Targets...>> {
	template <typename St> static constexpr Flow run(St & st) {
		const std::uint32_t value = eval_node<V, St>(st);
		if (st.raised) {
			return Flow::next;
		}
		// chained targets assign left to right, all from the one value
		(void)((assign_target<Targets>::run(st, value), !st.raised) && ...);
		return Flow::next;
	}
};

// aug-assign to a name or a subscript (attribute targets are out of
// the v0.1 subset)
template <typename Op, typename Target, typename V> struct aug_exec {
	static_assert(sizeof(Target) == 0,
		"ctpy: aug-assign to this target kind is not in the v0.1 subset");
};
template <typename Op, typename Text, typename V> struct aug_exec<Op, ast::name<Text>, V> {
	template <typename St> static constexpr Flow run(St & st) {
		const std::uint32_t current = st.lookup(Text::view());
		if (current == not_found) {
			st.raise_error(ex_kind::NameError, {"name '", Text::view(), "' is not defined"});
			return Flow::next;
		}
		const std::uint32_t value = eval_node<V, St>(st);
		if (st.raised) {
			return Flow::next;
		}
		const std::uint32_t result = bin_op(st, bop_of<Op>::value, current, value);
		if (!st.raised) {
			st.bind(Text::view(), result);
		}
		return Flow::next;
	}
};
// a[i] += v: evaluate the object and index ONCE, load, evaluate the
// right side, operate, store back through the same object/index
template <typename Op, typename Obj, typename Index, typename V>
struct aug_exec<Op, ast::subscript_expr<Obj, Index>, V> {
	template <typename St> static constexpr Flow run(St & st) {
		const std::uint32_t object = eval_node<Obj, St>(st);
		if (st.raised) {
			return Flow::next;
		}
		const std::uint32_t key = eval_node<Index, St>(st);
		if (st.raised) {
			return Flow::next;
		}
		const std::uint32_t current = subscript_load(st, object, key);
		if (st.raised) {
			return Flow::next;
		}
		const std::uint32_t value = eval_node<V, St>(st);
		if (st.raised) {
			return Flow::next;
		}
		const std::uint32_t result = bin_op(st, bop_of<Op>::value, current, value);
		if (!st.raised) {
			subscript_store(st, object, key, result);
		}
		return Flow::next;
	}
};

// aug-assign through a slice is out of the v0.1 subset, like slice stores
template <typename Op, typename Obj, typename L, typename U, typename S, typename V>
struct aug_exec<Op, ast::subscript_expr<Obj, ast::slice_expr<L, U, S>>, V> {
	template <typename St> static constexpr Flow run(St & st) {
		st.raise_error(ex_kind::TypeError, "ctpy v0.1: slice assignment is not supported");
		return Flow::next;
	}
};

template <typename Op, typename Target, typename V> struct executor<ast::aug_stmt<Op, Target, V>> {
	template <typename St> static constexpr Flow run(St & st) {
		return aug_exec<Op, Target, V>::run(st);
	}
};

// --- suites and compound statements -----------------------------------------------

template <typename... Stmts> struct executor<ast::suite<Stmts...>> {
	template <typename St> static constexpr Flow run(St & st) {
		Flow flow = Flow::next;
		(void)(((flow = exec_node<Stmts, St>(st)), flow == Flow::next && !st.raised) && ...);
		return flow;
	}
};

// an absent else-suite (the `void` slot) falls through
template <typename Else, typename St> constexpr Flow exec_else(St & st) {
	if constexpr (std::is_void_v<Else>) {
		return Flow::next;
	} else {
		return exec_node<Else, St>(st);
	}
}

template <typename Clauses, typename Else> struct elif_chain;
template <typename Else> struct elif_chain<ast::clause_pack<>, Else> {
	template <typename St> static constexpr Flow run(St & st) {
		return exec_else<Else>(st);
	}
};
template <typename Test, typename Body, typename... Rest, typename Else>
struct elif_chain<ast::clause_pack<ast::elif_clause<Test, Body>, Rest...>, Else> {
	template <typename St> static constexpr Flow run(St & st) {
		const std::uint32_t condition = eval_node<Test, St>(st);
		if (st.raised) {
			return Flow::next;
		}
		if (st.truthy(condition)) {
			return exec_node<Body, St>(st);
		}
		return elif_chain<ast::clause_pack<Rest...>, Else>::run(st);
	}
};

template <typename Test, typename Body, typename Elifs, typename Else>
struct executor<ast::if_stmt<Test, Body, Elifs, Else>> {
	template <typename St> static constexpr Flow run(St & st) {
		const std::uint32_t condition = eval_node<Test, St>(st);
		if (st.raised) {
			return Flow::next;
		}
		if (st.truthy(condition)) {
			return exec_node<Body, St>(st);
		}
		return elif_chain<Elifs, Else>::run(st);
	}
};

template <typename Test, typename Body, typename Else>
struct executor<ast::while_stmt<Test, Body, Else>> {
	template <typename St> static constexpr Flow run(St & st) {
		while (true) {
			const std::uint32_t condition = eval_node<Test, St>(st);
			if (st.raised) {
				return Flow::next;
			}
			if (!st.truthy(condition)) {
				break; // normal exhaustion: the else-suite runs
			}
			const Flow flow = exec_node<Body, St>(st);
			if (st.raised) {
				return Flow::next;
			}
			if (flow == Flow::break_) {
				return Flow::next; // break skips the else-suite
			}
			if (flow == Flow::return_) {
				return flow;
			}
			// Flow::next and Flow::continue_ both just iterate
		}
		return exec_else<Else>(st);
	}
};

template <typename Target, typename Iter, typename Body, typename Else>
struct executor<ast::for_stmt<Target, Iter, Body, Else>> {
	template <typename St> static constexpr Flow run(St & st) {
		const std::uint32_t iterable = eval_node<Iter, St>(st);
		if (st.raised) {
			return Flow::next;
		}
		const Object sequence = st.a.objs[iterable]; // copy: the pool grows under the loop
		if (!iterable_kind(sequence.kind)) {
			st.raise_error(ex_kind::TypeError,
				{"'", type_name(sequence.kind), "' object is not iterable"});
			return Flow::next;
		}
		const long long limit = iter_len(st, sequence);
		for (long long at = 0; at < limit; ++at) {
			const std::uint32_t element = iter_get(st, sequence, at);
			assign_target<Target>::run(st, element);
			if (st.raised) {
				return Flow::next;
			}
			const Flow flow = exec_node<Body, St>(st);
			if (st.raised) {
				return Flow::next;
			}
			if (flow == Flow::break_) {
				return Flow::next; // break skips the else-suite
			}
			if (flow == Flow::return_) {
				return flow;
			}
		}
		return exec_else<Else>(st);
	}
};

// --- def / call: the function machinery -----------------------------------

template <typename P> struct param_traits;
template <typename N, typename D> struct param_traits<ast::param<N, D>> {
	static constexpr bool has_default = !std::is_void_v<D>;
	using default_expr = D;
	static constexpr std::string_view name() noexcept {
		return N::view();
	}
};

// evaluate one parameter's default at DEF time (Python: defaults are
// evaluated once, when the def statement executes - never per call)
template <typename P, typename St>
constexpr bool eval_default(St & st, std::uint32_t * items, std::size_t & used) {
	if constexpr (param_traits<P>::has_default) {
		items[used++] = eval_node<typename param_traits<P>::default_expr, St>(st);
		return !st.raised;
	} else {
		(void)st;
		(void)items;
		(void)used;
		return true;
	}
}

// The M5 type-erasure choice (PLAN.md section 5): a TABLE OF THUNKS,
// not a variadic module walk. Executing a def instantiates
// fn_thunk<Def, St>::call - a constexpr function that owns everything
// type-level about the def (parameter names, the body suite) - and
// registers its POINTER in State::thunks; the function Object carries
// only that index plus its defaults run. A call site (builtins.hpp)
// dispatches through the pointer without ever naming the def's type.
// Chosen over re-walking the module's defs variadically per call
// because the walk is O(defs) at every call, cannot see functions
// created at run time (nested def, conditional def, aliases), and
// would entangle the call evaluator with the module type; the pointer
// also breaks template mutual recursion by construction - recursive
// and mutually-recursive calls cross a VALUE, not a type.
template <typename Def, typename St> struct fn_thunk;
template <typename NameText, typename... Ps, typename Body, typename St>
struct fn_thunk<ast::def_stmt<NameText, ast::param_pack<Ps...>, Body>, St> {
	static constexpr std::size_t param_count = sizeof...(Ps);
	static constexpr std::string_view param_names[param_count + 1] = {param_traits<Ps>::name()..., {}};

	// CPython's wrong-arity TypeErrors, spelled the way a traceback would
	static constexpr std::uint32_t arity_error(St & st, const Object & fn, std::size_t argc, std::size_t required) {
		const auto given = dec(static_cast<long long>(argc));
		if (argc > param_count) {
			const auto most = dec(static_cast<long long>(param_count));
			if (fn.count == 0) {
				return st.raise_error(ex_kind::TypeError,
					{NameText::view(), "() takes ", most.view(), " positional argument",
					 param_count == 1 ? "" : "s", " but ", given.view(),
					 argc == 1 ? " was" : " were", " given"});
			}
			const auto least = dec(static_cast<long long>(required));
			return st.raise_error(ex_kind::TypeError,
				{NameText::view(), "() takes from ", least.view(), " to ", most.view(),
				 " positional arguments but ", given.view(), argc == 1 ? " was" : " were", " given"});
		}
		const auto missing = dec(static_cast<long long>(required - argc));
		st.raise_error(ex_kind::TypeError,
			{NameText::view(), "() missing ", missing.view(), " required positional argument",
			 required - argc == 1 ? "" : "s", ": "});
		for (std::size_t at = argc; at < required; ++at) {
			if (at != argc) {
				st.error.append(at + 1 == required ? (required - argc == 2 ? " and " : ", and ") : ", ");
			}
			st.error.append("'").append(param_names[at]).append("'");
		}
		return st.none();
	}

	static constexpr std::uint32_t call(St & st, const Object & fn, const std::uint32_t * argv, std::size_t argc) {
		// the SOFT recursion guard: counts live Python calls and raises
		// RecursionError (default limit 100) long before the compiler's
		// own -fconstexpr-depth budget could hard-fail the build
		if (st.depth >= st.recursion_limit) {
			return st.raise_error(ex_kind::RecursionError, "maximum recursion depth exceeded");
		}
		const std::size_t required = param_count - fn.count;
		if (argc < required || argc > param_count) {
			return arity_error(st, fn, argc, required);
		}
		++st.depth;
		st.push_frame();
		// bind parameters left to right: caller-supplied first, then the
		// def-time defaults [fn.first, fn.first+fn.count) fill the tail
		std::size_t at = 0;
		((st.bind(param_traits<Ps>::name(),
		          at < argc ? argv[at] : fn.first + static_cast<std::uint32_t>(at - required)),
		  ++at), ...);
		(void)at;
		const Flow flow = exec_node<Body, St>(st);
		st.pop_frame();
		--st.depth;
		if (st.raised) {
			return st.none();
		}
		switch (flow) {
			case Flow::return_:
				return st.retval;
			case Flow::break_: // grammar superset: the semantic check lives here
				return st.raise_error(ex_kind::SyntaxError, "'break' outside loop");
			case Flow::continue_:
				return st.raise_error(ex_kind::SyntaxError, "'continue' not properly in loop");
			case Flow::next:
				break;
		}
		return st.none(); // fell off the end: an implicit `return None`
	}
};

// Executing a def EVALUATES ITS DEFAULTS NOW (Python semantics: once,
// at def time), copies them into one contiguous pool run, registers
// the body's thunk, and binds the name like any other assignment.
// A nested def binds in the enclosing call's locals, so the inner name
// dies with the call; when the inner function runs it sees only its
// OWN locals + globals (v0.1 has NO closures - an enclosing function's
// locals are invisible by scope-resolution design, documented).
template <typename NameText, typename... Ps, typename Body>
struct executor<ast::def_stmt<NameText, ast::param_pack<Ps...>, Body>> {
	template <typename St> static constexpr Flow run(St & st) {
		std::uint32_t items[sizeof...(Ps) + 1]{};
		std::size_t used = 0;
		const bool complete = (eval_default<Ps, St>(st, items, used) && ...);
		(void)complete;
		if (st.raised) {
			return Flow::next;
		}
		// the defaults run: contiguous copies, like any container display
		const std::uint32_t first = static_cast<std::uint32_t>(st.a.objs.size());
		for (std::size_t at = 0; at < used; ++at) {
			st.a.objs.push_back(st.a.objs[items[at]]);
		}
		// register the thunk; a def that executes again (loop, recursion,
		// redefinition) reuses its slot - the table stays one-per-def
		using thunk = fn_thunk<ast::def_stmt<NameText, ast::param_pack<Ps...>, Body>, St>;
		const typename St::function_thunk pointer = &thunk::call;
		std::size_t index = st.thunks.size();
		for (std::size_t slot = 0; slot < st.thunks.size(); ++slot) {
			if (st.thunks[slot] == pointer) {
				index = slot;
				break;
			}
		}
		if (index == st.thunks.size()) {
			st.thunks.push_back(pointer);
		}
		st.bind(NameText::view(), st.push(Object{.kind = Kind::function,
		                                         .i = static_cast<long long>(index),
		                                         .first = first,
		                                         .count = static_cast<std::uint32_t>(used)}));
		return Flow::next;
	}
};

// the module body; a control transfer that escapes to module level is
// the SyntaxError CPython reports (soft - the grammar accepts the
// statement anywhere, the semantic check lives here)
template <typename... Stmts> struct executor<ast::module<Stmts...>> {
	template <typename St> static constexpr Flow run(St & st) {
		Flow flow = Flow::next;
		(void)(((flow = exec_node<Stmts, St>(st)), flow == Flow::next && !st.raised) && ...);
		if (!st.raised) {
			switch (flow) {
				case Flow::next: break;
				case Flow::break_:
					st.raise_error(ex_kind::SyntaxError, "'break' outside loop");
					break;
				case Flow::continue_:
					st.raise_error(ex_kind::SyntaxError, "'continue' not properly in loop");
					break;
				case Flow::return_:
					st.raise_error(ex_kind::SyntaxError, "'return' outside function");
					break;
			}
		}
		return Flow::next;
	}
};

} // namespace detail

// execute one statement (or suite, or whole module) inside a live
// interpreter state, returning how control left it
CTPY_EXPORT template <typename Stmt, typename St> constexpr Flow exec(St & st) {
	return detail::exec_node<Stmt, St>(st);
}

} // namespace ctpy

#endif
