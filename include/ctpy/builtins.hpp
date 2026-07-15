#ifndef CTPY__BUILTINS__HPP
#define CTPY__BUILTINS__HPP

#include "version.hpp"
#include "text.hpp"
#include "ast.hpp"
#include "object.hpp"
#include "eval.hpp"

#ifndef CTPY_IN_A_MODULE
#include <cstddef>
#include <cstdint>
#include <string_view>
#endif

// The compile-time builtins and every call site's dispatch. M4 shipped
// range() - a real lazy range OBJECT (start/stop/step ints in the pool,
// Kind::range), not a materialized list, so `for i in range(1000)`
// costs three pool slots, not a thousand. M6 adds len() and the minimal
// METHOD set (list.append, dict.keys/.values/.items/.get). The rest of
// the v0.1 builtin set (print, sum, min, max, abs, str, int, bool,
// sorted, enumerate, zip) lands in M7.
//
// Calling a NAME dispatches here: a bound name wins (Python lets you
// shadow builtins), an unbound one is looked up in the builtin table,
// anything else is a NameError. A bound FUNCTION object (made by a def
// statement, exec.hpp) is invoked through its type-erased thunk in
// State::thunks - this call site never sees the def's AST type.
// Calling an ATTRIBUTE (obj.method(...)) dispatches on the object's
// Kind plus the method name - an unknown pair is an AttributeError.
// Keyword arguments are OUT of the v0.1 call subset (calls are
// positional-only; a kwarg raises a soft TypeError saying so).

namespace ctpy {

namespace detail {

// --- range() ---------------------------------------------------------------

template <typename St>
constexpr std::uint32_t builtin_range(St & st, const std::uint32_t * argv, std::size_t argc) {
	if (argc == 0) {
		return st.raise_error(ex_kind::TypeError, "range expected at least 1 argument, got 0");
	}
	if (argc > 3) {
		const auto got = dec(static_cast<long long>(argc));
		return st.raise_error(ex_kind::TypeError,
			{"range expected at most 3 arguments, got ", got.view()});
	}
	long long parts[3]{};
	for (std::size_t at = 0; at < argc; ++at) {
		const Object & argument = st.a.objs[argv[at]];
		if (!is_int_like(argument.kind)) {
			return st.raise_error(ex_kind::TypeError,
				{"'", type_name(argument.kind), "' object cannot be interpreted as an integer"});
		}
		parts[at] = argument.i;
	}
	const long long start = argc >= 2 ? parts[0] : 0;
	const long long stop = argc >= 2 ? parts[1] : parts[0];
	const long long step = argc == 3 ? parts[2] : 1;
	if (step == 0) {
		return st.raise_error(ex_kind::ValueError, "range() arg 3 must not be zero");
	}
	return make_range(st, start, stop, step);
}

// --- len() -------------------------------------------------------------------

template <typename St>
constexpr std::uint32_t builtin_len(St & st, const std::uint32_t * argv, std::size_t argc) {
	if (argc != 1) {
		const auto got = dec(static_cast<long long>(argc));
		return st.raise_error(ex_kind::TypeError,
			{"len() takes exactly one argument (", got.view(), " given)"});
	}
	const Object & object = st.a.objs[argv[0]];
	if (!iterable_kind(object.kind)) {
		return st.raise_error(ex_kind::TypeError,
			{"object of type '", type_name(object.kind), "' has no len()"});
	}
	return st.make_int(iter_len(st, object));
}

// --- the builtin table -------------------------------------------------------

constexpr bool is_builtin(std::string_view name) noexcept {
	return name == "range" || name == "len";
}

template <typename St>
constexpr std::uint32_t call_builtin(St & st, std::string_view name,
                                     const std::uint32_t * argv, std::size_t argc) {
	if (name == "range") {
		return builtin_range(st, argv, argc);
	}
	if (name == "len") {
		return builtin_len(st, argv, argc);
	}
	return st.raise_error(ex_kind::NameError, {"name '", name, "' is not defined"});
}

// --- methods (the minimal v0.1 set) --------------------------------------------

// dict.keys()/.values()/.items() return materialized LISTS (v0.1 has no
// view objects; CPython code that iterates or len()s them works the
// same). items() lays out the element runs first (key/value copies,
// pairwise), then the tuple headers as one contiguous run - which IS
// the list's element run.
template <typename St>
constexpr std::uint32_t dict_view_list(St & st, const Object & self, int which) {
	if (which == 2) {
		const std::uint32_t base = static_cast<std::uint32_t>(st.a.objs.size());
		for (std::uint32_t at = 0; at < self.count; ++at) {
			const Pair entry = st.a.pairs[self.first + at];
			st.a.objs.push_back(st.a.objs[entry.key]);
			st.a.objs.push_back(st.a.objs[entry.value]);
		}
		const std::uint32_t headers = static_cast<std::uint32_t>(st.a.objs.size());
		for (std::uint32_t at = 0; at < self.count; ++at) {
			st.a.objs.push_back(Object{.kind = Kind::tuple, .first = base + 2 * at, .count = 2});
		}
		return st.push(Object{.kind = Kind::list, .first = headers, .count = self.count});
	}
	const std::uint32_t first = static_cast<std::uint32_t>(st.a.objs.size());
	for (std::uint32_t at = 0; at < self.count; ++at) {
		const Pair entry = st.a.pairs[self.first + at];
		st.a.objs.push_back(st.a.objs[which == 0 ? entry.key : entry.value]);
	}
	return st.push(Object{.kind = Kind::list, .first = first, .count = self.count});
}

template <typename St>
constexpr std::uint32_t call_method(St & st, std::uint32_t self, std::string_view name,
                                    const std::uint32_t * argv, std::size_t argc) {
	const Object object = st.a.objs[self]; // copy: methods below may grow the pool
	if (object.kind == Kind::list && name == "append") {
		if (argc != 1) {
			const auto got = dec(static_cast<long long>(argc));
			return st.raise_error(ex_kind::TypeError,
				{"list.append() takes exactly one argument (", got.view(), " given)"});
		}
		list_append(st, self, argv[0]);
		return st.none();
	}
	if (object.kind == Kind::dict) {
		const int which = name == "keys" ? 0 : name == "values" ? 1 : name == "items" ? 2 : -1;
		if (which >= 0) {
			if (argc != 0) {
				const auto got = dec(static_cast<long long>(argc));
				return st.raise_error(ex_kind::TypeError,
					{name, "() takes no arguments (", got.view(), " given)"});
			}
			return dict_view_list(st, object, which);
		}
		if (name == "get") {
			if (argc < 1) {
				return st.raise_error(ex_kind::TypeError, "get expected at least 1 argument, got 0");
			}
			if (argc > 2) {
				const auto got = dec(static_cast<long long>(argc));
				return st.raise_error(ex_kind::TypeError,
					{"get expected at most 2 arguments, got ", got.view()});
			}
			if (!hashable(st, argv[0])) {
				return st.raise_error(ex_kind::TypeError,
					{"unhashable type: '", type_name(st.a.objs[argv[0]].kind), "'"});
			}
			for (std::uint32_t at = 0; at < object.count; ++at) {
				if (object_eq(st, argv[0], st.a.pairs[object.first + at].key)) {
					return st.a.pairs[object.first + at].value;
				}
			}
			return argc == 2 ? argv[1] : st.none();
		}
	}
	return st.raise_error(ex_kind::AttributeError,
		{"'", type_name(object.kind), "' object has no attribute '", name, "'"});
}

// --- calling a name -------------------------------------------------------------

template <typename Text, typename... Args>
struct evaluator<ast::call_expr<ast::name<Text>, Args...>> {
	template <typename St> static constexpr std::uint32_t run(St & st) {
		const std::uint32_t bound = st.lookup(Text::view());
		if (bound != not_found && st.a.objs[bound].kind != Kind::function) {
			// a binding shadows any builtin, and only function objects
			// (and builtins) are callable in the v0.1 subset
			return st.raise_error(ex_kind::TypeError,
				{"'", type_name(st.a.objs[bound].kind), "' object is not callable"});
		}
		if (bound == not_found && !is_builtin(Text::view())) {
			return st.raise_error(ex_kind::NameError,
				{"name '", Text::view(), "' is not defined"});
		}
		if constexpr ((is_kwarg<Args> || ...)) {
			return st.raise_error(ex_kind::TypeError,
				{Text::view(), "() takes no keyword arguments (ctpy v0.1: calls are positional-only)"});
		} else {
			// copy the callee OBJECT before the arguments grow the pool
			const Object fn = bound != not_found ? st.a.objs[bound] : Object{};
			std::uint32_t argv[sizeof...(Args) + 1]{};
			std::size_t at = 0;
			const bool complete = ((argv[at++] = eval_node<Args, St>(st), !st.raised) && ...);
			(void)complete;
			(void)at;
			if (st.raised) {
				return st.none();
			}
			if (bound != not_found) {
				// a def'd function: dispatch through its type-erased thunk
				return st.thunks[static_cast<std::size_t>(fn.i)](st, fn, argv, sizeof...(Args));
			}
			return call_builtin(st, Text::view(), argv, sizeof...(Args));
		}
	}
};

// --- calling a method: obj.name(args...) --------------------------------------

template <typename Obj, typename NameText, typename... Args>
struct evaluator<ast::call_expr<ast::attribute_expr<Obj, NameText>, Args...>> {
	template <typename St> static constexpr std::uint32_t run(St & st) {
		const std::uint32_t self = eval_node<Obj, St>(st);
		if (st.raised) {
			return st.none();
		}
		if constexpr ((is_kwarg<Args> || ...)) {
			return st.raise_error(ex_kind::TypeError,
				{NameText::view(), "() takes no keyword arguments (ctpy v0.1: calls are positional-only)"});
		} else {
			std::uint32_t argv[sizeof...(Args) + 1]{};
			std::size_t at = 0;
			const bool complete = ((argv[at++] = eval_node<Args, St>(st), !st.raised) && ...);
			(void)complete;
			(void)at;
			if (st.raised) {
				return st.none();
			}
			return call_method(st, self, NameText::view(), argv, sizeof...(Args));
		}
	}
};

} // namespace detail

} // namespace ctpy

#endif
