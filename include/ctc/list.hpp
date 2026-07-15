#ifndef CTC__LIST__HPP
#define CTC__LIST__HPP

#include "utility.hpp"

#ifndef CTC_IN_A_MODULE
#include <type_traits>
#endif

// A fixed-capacity constexpr doubly-linked list (std::list): nodes in
// a plain array linked by INDICES, same design and same NTTP caveat as
// forward_list.hpp - node placement depends on the operation history,
// so only == is content-based; with_capacity()/shrunk<V>() rebuilds
// canonically.

namespace ctc {

CTC_EXPORT template <typename T, size_t N> struct list {
	static_assert(std::is_default_constructible_v<T>, "ctc::list<T, N> requires a default-constructible T");

	using value_type = T;
	using size_type = size_t;

	static constexpr size_type nil = static_cast<size_type>(-1);

	struct node {
		T value{};
		size_type prev{nil};
		size_type next{nil};
	};

	// vacated nodes are reset to T{} and threaded on the free list
	// (through next)
	node content[N ? N : 1]{};
	size_type head{nil};
	size_type tail{nil};
	size_type free_head{0};
	size_type real_size{0};

	constexpr list() noexcept {
		for (size_type i{0}; i < N; ++i) {
			content[i].next = i + 1 < N ? i + 1 : nil;
		}
		if (N == 0) {
			free_head = nil;
		}
	}

	struct const_iterator {
		const list * container{nullptr};
		size_type index{nil};

		constexpr const T & operator*() const noexcept {
			return container->content[index].value;
		}
		constexpr const T * operator->() const noexcept {
			return &container->content[index].value;
		}
		constexpr const_iterator & operator++() noexcept {
			index = container->content[index].next;
			return *this;
		}
		constexpr const_iterator operator++(int) noexcept {
			const_iterator before = *this;
			++*this;
			return before;
		}
		// --end() is the last element, like std
		constexpr const_iterator & operator--() noexcept {
			index = index == nil ? container->tail : container->content[index].prev;
			return *this;
		}
		constexpr const_iterator operator--(int) noexcept {
			const_iterator before = *this;
			--*this;
			return before;
		}
		friend constexpr bool operator==(const const_iterator &, const const_iterator &) noexcept = default;
	};
	struct iterator {
		list * container{nullptr};
		size_type index{nil};

		constexpr T & operator*() const noexcept {
			return container->content[index].value;
		}
		constexpr T * operator->() const noexcept {
			return &container->content[index].value;
		}
		constexpr iterator & operator++() noexcept {
			index = container->content[index].next;
			return *this;
		}
		constexpr iterator operator++(int) noexcept {
			iterator before = *this;
			++*this;
			return before;
		}
		constexpr iterator & operator--() noexcept {
			index = index == nil ? container->tail : container->content[index].prev;
			return *this;
		}
		constexpr iterator operator--(int) noexcept {
			iterator before = *this;
			--*this;
			return before;
		}
		constexpr operator const_iterator() const noexcept {
			return {container, index};
		}
		friend constexpr bool operator==(const iterator &, const iterator &) noexcept = default;
	};

	// observers
	constexpr size_type size() const noexcept {
		return real_size;
	}
	static constexpr size_type capacity() noexcept {
		return N;
	}
	static constexpr size_type max_size() noexcept {
		return N;
	}
	constexpr bool empty() const noexcept {
		return real_size == 0;
	}
	constexpr bool full() const noexcept {
		return real_size == N;
	}

	constexpr iterator begin() noexcept {
		return {this, head};
	}
	constexpr const_iterator begin() const noexcept {
		return {this, head};
	}
	constexpr iterator end() noexcept {
		return {this, nil};
	}
	constexpr const_iterator end() const noexcept {
		return {this, nil};
	}

	constexpr T & front() noexcept {
		if (real_size == 0) {
			detail::precondition_violated("ctc::list: front() on an empty list");
		}
		return content[head].value;
	}
	constexpr const T & front() const noexcept {
		if (real_size == 0) {
			detail::precondition_violated("ctc::list: front() on an empty list");
		}
		return content[head].value;
	}
	constexpr T & back() noexcept {
		if (real_size == 0) {
			detail::precondition_violated("ctc::list: back() on an empty list");
		}
		return content[tail].value;
	}
	constexpr const T & back() const noexcept {
		if (real_size == 0) {
			detail::precondition_violated("ctc::list: back() on an empty list");
		}
		return content[tail].value;
	}

	// mutation
	constexpr void push_front(const T & value) {
		insert(begin(), value);
	}
	constexpr void push_back(const T & value) {
		insert(end(), value);
	}
	constexpr void pop_front() {
		if (real_size == 0) {
			detail::precondition_violated("ctc::list: pop_front on an empty list");
		}
		erase(begin());
	}
	constexpr void pop_back() {
		if (real_size == 0) {
			detail::precondition_violated("ctc::list: pop_back on an empty list");
		}
		erase({this, tail});
	}
	// inserts before `position` (std::list semantics); returns an
	// iterator to the new element
	constexpr iterator insert(const_iterator position, const T & value) {
		if (free_head == nil) {
			detail::precondition_violated("ctc::list: insertion into a full list");
		}
		const size_type index = free_head;
		free_head = content[index].next;
		++real_size;
		content[index].value = value;
		const size_type after = position.index;
		const size_type before = after == nil ? tail : content[after].prev;
		content[index].prev = before;
		content[index].next = after;
		if (before == nil) {
			head = index;
		} else {
			content[before].next = index;
		}
		if (after == nil) {
			tail = index;
		} else {
			content[after].prev = index;
		}
		return {this, index};
	}
	// erases `position`; returns an iterator to the element after it
	constexpr iterator erase(const_iterator position) {
		if (position.index == nil) {
			detail::precondition_violated("ctc::list: erase(end())");
		}
		const size_type index = position.index;
		const size_type before = content[index].prev;
		const size_type after = content[index].next;
		if (before == nil) {
			head = after;
		} else {
			content[before].next = after;
		}
		if (after == nil) {
			tail = before;
		} else {
			content[after].prev = before;
		}
		content[index].value = T{};
		content[index].prev = nil;
		content[index].next = free_head;
		free_head = index;
		--real_size;
		return {this, after};
	}
	constexpr void clear() {
		while (real_size != 0) {
			pop_front();
		}
	}

	// a compact, canonically-laid-out copy (this is also how a list
	// becomes NTTP-identity-safe)
	template <size_t M> constexpr list<T, M> with_capacity() const {
		if (real_size > M) {
			detail::precondition_violated("ctc::list: content does not fit the new capacity");
		}
		list<T, M> result;
		for (size_type i{head}; i != nil; i = content[i].next) {
			result.push_back(content[i].value);
		}
		return result;
	}
};

// equality: element-wise in list order, across capacities
CTC_EXPORT template <typename T, size_t A, size_t B> constexpr bool operator==(const list<T, A> & lhs, const list<T, B> & rhs) {
	if (lhs.size() != rhs.size()) {
		return false;
	}
	auto left = lhs.begin();
	auto right = rhs.begin();
	for (; left != lhs.end(); ++left, ++right) {
		if (!(*left == *right)) {
			return false;
		}
	}
	return true;
}

} // namespace ctc

#endif
