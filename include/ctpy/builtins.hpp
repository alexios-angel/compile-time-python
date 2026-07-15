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

// The compile-time builtins. M4 ships only range() - a real lazy range
// OBJECT (start/stop/step ints in the pool, Kind::range), not a
// materialized list, so `for i in range(1000)` costs three pool slots,
// not a thousand. The rest of the v0.1 builtin set (print, len, sum,
// min, max, abs, str, int, bool, sorted, enumerate, zip) lands in M7.
//
// Calling a NAME dispatches here: a bound name wins (Python lets you
// shadow builtins), an unbound one is looked up in the builtin table,
// anything else is a NameError. User-defined function calls land in M5.

namespace ctpy {

namespace detail {

// spell a small count for an error message ("expected 2, got 3")
constexpr ctc::string<20> dec(long long value) noexcept {
	ctc::string<20> out{};
	unsigned long long magnitude = 0;
	if (value < 0) {
		out.push_back('-');
		magnitude = 0ULL - static_cast<unsigned long long>(value);
	} else {
		magnitude = static_cast<unsigned long long>(value);
	}
	char digits[20]{};
	std::size_t used = 0;
	do {
		digits[used++] = static_cast<char>('0' + static_cast<char>(magnitude % 10ULL));
		magnitude /= 10ULL;
	} while (magnitude != 0);
	while (used > 0) {
		out.push_back(digits[--used]);
	}
	return out;
}

// --- range() ---------------------------------------------------------------

template <typename St>
constexpr std::uint32_t make_range(St & st, long long start, long long stop, long long step) {
	const std::uint32_t first = static_cast<std::uint32_t>(st.a.objs.size());
	st.make_int(start);
	st.make_int(stop);
	st.make_int(step);
	return st.push(Object{.kind = Kind::range, .first = first, .count = 3});
}

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

// --- the builtin table -------------------------------------------------------

constexpr bool is_builtin(std::string_view name) noexcept {
	return name == "range";
}

template <typename St>
constexpr std::uint32_t call_builtin(St & st, std::string_view name,
                                     const std::uint32_t * argv, std::size_t argc) {
	if (name == "range") {
		return builtin_range(st, argv, argc);
	}
	return st.raise_error(ex_kind::NameError, {"name '", name, "' is not defined"});
}

// --- calling a name -------------------------------------------------------------

template <typename Text, typename... Args>
struct evaluator<ast::call_expr<ast::name<Text>, Args...>> {
	template <typename St> static constexpr std::uint32_t run(St & st) {
		const std::uint32_t bound = st.lookup(Text::view());
		if (bound != not_found) {
			// a binding shadows any builtin; calling def-functions lands
			// in M5, everything else was never callable
			return st.raise_error(ex_kind::TypeError,
				{"'", type_name(st.a.objs[bound].kind), "' object is not callable"});
		}
		if (!is_builtin(Text::view())) {
			return st.raise_error(ex_kind::NameError,
				{"name '", Text::view(), "' is not defined"});
		}
		if constexpr ((is_kwarg<Args> || ...)) {
			return st.raise_error(ex_kind::TypeError,
				{Text::view(), "() takes no keyword arguments"});
		} else {
			std::uint32_t argv[sizeof...(Args) + 1]{};
			std::size_t at = 0;
			const bool complete = ((argv[at++] = eval_node<Args, St>(st), !st.raised) && ...);
			(void)complete;
			(void)at;
			if (st.raised) {
				return st.none();
			}
			return call_builtin(st, Text::view(), argv, sizeof...(Args));
		}
	}
};

} // namespace detail

} // namespace ctpy

#endif
