#ifndef CTC__UTILITY__HPP
#define CTC__UTILITY__HPP

#ifndef CTC_IN_A_MODULE
#include <cstddef>
#include <cstdlib>
#endif

#ifdef CTC_IN_A_MODULE
#define CTC_EXPORT export
#else
#define CTC_EXPORT
#endif

namespace ctc {

using std::size_t;

// transparent, stateless functors: the associative containers default
// to these and construct them on use instead of storing them (a
// stored comparator would be one more member in every NTTP)
CTC_EXPORT struct less {
	using is_transparent = void;
	template <typename A, typename B> constexpr bool operator()(const A & a, const B & b) const {
		return a < b;
	}
};

CTC_EXPORT struct equal_to {
	using is_transparent = void;
	template <typename A, typename B> constexpr bool operator()(const A & a, const B & b) const {
		return a == b;
	}
};

namespace detail {

// The precondition trap. Calling a non-constexpr function is not a
// constant expression, so inside constant evaluation a violated
// precondition fails the compilation - with this function's name and
// its string argument visible in the constexpr backtrace. At runtime
// it aborts (the library is exception-free).
[[noreturn]] inline void precondition_violated(const char *) noexcept {
	std::abort();
}

// binary search over the sorted containers' storage. KeyOf projects an
// element to its key (identity for sets, .first for maps).
template <typename Compare, typename Node, typename K, typename KeyOf>
constexpr size_t lower_bound_index(const Node * data, size_t size, const K & key, const KeyOf & key_of) noexcept {
	size_t low{0};
	size_t high{size};
	while (low < high) {
		const size_t mid = low + (high - low) / 2;
		if (Compare{}(key_of(data[mid]), key)) {
			low = mid + 1;
		} else {
			high = mid;
		}
	}
	return low;
}

template <typename Compare, typename Node, typename K, typename KeyOf>
constexpr size_t upper_bound_index(const Node * data, size_t size, const K & key, const KeyOf & key_of) noexcept {
	size_t low{0};
	size_t high{size};
	while (low < high) {
		const size_t mid = low + (high - low) / 2;
		if (!Compare{}(key, key_of(data[mid]))) {
			low = mid + 1;
		} else {
			high = mid;
		}
	}
	return low;
}

} // namespace detail

} // namespace ctc

#endif
