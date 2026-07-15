#ifndef CTC__STACK__HPP
#define CTC__STACK__HPP

#include "utility.hpp"
#include "vector.hpp"

// std::stack over a ctc container. The underlying container is a
// public member (std keeps it protected; public keeps the adaptor a
// structural type), named c like std's.

namespace ctc {

CTC_EXPORT template <typename T, size_t N, typename Container = vector<T, N>> struct stack {
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
			detail::precondition_violated("ctc::stack: pop on an empty stack");
		}
		c.pop_back();
	}
	constexpr reference top() {
		return c.back();
	}
	constexpr const_reference top() const {
		return c.back();
	}

	friend constexpr bool operator==(const stack &, const stack &) = default;
};

} // namespace ctc

#endif
