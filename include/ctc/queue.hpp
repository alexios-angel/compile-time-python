#ifndef CTC__QUEUE__HPP
#define CTC__QUEUE__HPP

#include "utility.hpp"
#include "deque.hpp"

#ifndef CTC_IN_A_MODULE
#include <type_traits>
#endif

// std::queue and std::priority_queue over ctc containers. The
// underlying container is a public member (std keeps it protected;
// public keeps the adaptors structural), named c like std's.

namespace ctc {

CTC_EXPORT template <typename T, size_t N, typename Container = deque<T, N>> struct queue {
	using container_type = Container;
	using value_type = T;
	using size_type = size_t;
	using reference = T &;
	using const_reference = const T &;

	Container c{};

	constexpr size_type size() const noexcept {
		return c.size();
	}
	static constexpr size_type capacity() noexcept {
		return Container::capacity();
	}
	constexpr bool empty() const noexcept {
		return c.empty();
	}
	constexpr bool full() const noexcept {
		return c.full();
	}

	constexpr void push(const T & value) {
		c.push_back(value);
	}
	template <typename... Args> constexpr reference emplace(Args &&... args) {
		return c.emplace_back(static_cast<Args &&>(args)...);
	}
	constexpr void pop() {
		if (c.empty()) {
			detail::precondition_violated("ctc::queue: pop on an empty queue");
		}
		c.pop_front();
	}
	constexpr reference front() {
		return c.front();
	}
	constexpr const_reference front() const {
		return c.front();
	}
	constexpr reference back() {
		return c.back();
	}
	constexpr const_reference back() const {
		return c.back();
	}

	friend constexpr bool operator==(const queue &, const queue &) = default;
};

// std::priority_queue: a binary max-heap (with the default ctc::less,
// top() is the largest element, like std). The heap layout depends on
// the push/pop history, so unlike the canonical-layout containers,
// equal contents do NOT guarantee equivalent template arguments here -
// only operator== equality of the underlying storage.
CTC_EXPORT template <typename T, size_t N, typename Compare = less, typename Container = vector<T, N>> struct priority_queue {
	static_assert(std::is_empty_v<Compare>, "ctc::priority_queue: Compare must be stateless (it is not stored)");

	using container_type = Container;
	using value_type = T;
	using size_type = size_t;
	using const_reference = const T &;
	using value_compare = Compare;

	Container c{};

	constexpr size_type size() const noexcept {
		return c.size();
	}
	static constexpr size_type capacity() noexcept {
		return Container::capacity();
	}
	constexpr bool empty() const noexcept {
		return c.empty();
	}
	constexpr bool full() const noexcept {
		return c.full();
	}

	constexpr const_reference top() const {
		return c.front();
	}

	constexpr void push(const T & value) {
		c.push_back(value);
		// sift up
		size_type i = c.size() - 1;
		while (i > 0) {
			const size_type parent = (i - 1) / 2;
			if (Compare{}(c[parent], c[i])) {
				const T temporary = c[parent];
				c[parent] = c[i];
				c[i] = temporary;
				i = parent;
			} else {
				break;
			}
		}
	}
	constexpr void pop() {
		if (c.empty()) {
			detail::precondition_violated("ctc::priority_queue: pop on an empty priority_queue");
		}
		c[0] = c[c.size() - 1];
		c.pop_back();
		// sift down
		size_type i = 0;
		while (true) {
			const size_type left = 2 * i + 1;
			const size_type right = 2 * i + 2;
			size_type largest = i;
			if (left < c.size() && Compare{}(c[largest], c[left])) {
				largest = left;
			}
			if (right < c.size() && Compare{}(c[largest], c[right])) {
				largest = right;
			}
			if (largest == i) {
				break;
			}
			const T temporary = c[i];
			c[i] = c[largest];
			c[largest] = temporary;
			i = largest;
		}
	}
};

} // namespace ctc

#endif
