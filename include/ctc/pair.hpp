#ifndef CTC__PAIR__HPP
#define CTC__PAIR__HPP

#include "utility.hpp"

namespace ctc {

// A structural std::pair: two public members and nothing else, so a
// pair (and any container of pairs) can be a non-type template
// parameter, which std::pair is not guaranteed to support. Aggregate:
// works with designated initializers and structured bindings.
CTC_EXPORT template <typename First, typename Second> struct pair {
	using first_type = First;
	using second_type = Second;

	First first{};
	Second second{};

	friend constexpr bool operator==(const pair &, const pair &) = default;
};

template <typename First, typename Second> pair(First, Second) -> pair<First, Second>;

CTC_EXPORT template <typename First, typename Second> constexpr pair<First, Second> make_pair(First first, Second second) noexcept {
	return {first, second};
}

} // namespace ctc

#endif
