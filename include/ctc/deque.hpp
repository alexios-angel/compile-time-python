#ifndef CTC__DEQUE__HPP
#define CTC__DEQUE__HPP

#include "utility.hpp"
#include "vector.hpp"

// A fixed-capacity constexpr deque. NOT a ring buffer: the front is
// always content[0], because a ring's head offset would make equal
// contents different template arguments (NTTP identity is layout
// identity). The price is O(size) front operations - irrelevant at
// compile-time sizes; the payoff is contiguous iteration and the same
// canonical-layout guarantee as every other ctc container. Publicly
// derives from ctc::vector (public bases keep a type structural), so
// the whole vector API is available.

namespace ctc {

CTC_EXPORT template <typename T, size_t N> struct deque : vector<T, N> {
	using vector<T, N>::vector;

	constexpr void push_front(const T & value) {
		if (this->size() == N) {
			detail::precondition_violated("ctc::deque: push_front on a full deque");
		}
		this->insert(this->begin(), value);
	}
	constexpr void pop_front() {
		if (this->empty()) {
			detail::precondition_violated("ctc::deque: pop_front on an empty deque");
		}
		this->erase(this->begin());
	}

	// a copy with a different capacity (shadows vector's, so shrunk<V>()
	// on a deque stays a deque)
	template <size_t M> constexpr deque<T, M> with_capacity() const {
		if (this->size() > M) {
			detail::precondition_violated("ctc::deque: content does not fit the new capacity");
		}
		deque<T, M> result;
		for (size_t i{0}; i < this->size(); ++i) {
			result.content[i] = this->content[i];
		}
		result.real_size = this->size();
		return result;
	}
};

} // namespace ctc

#endif
