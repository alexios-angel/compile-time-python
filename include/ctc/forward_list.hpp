#ifndef CTC__FORWARD_LIST__HPP
#define CTC__FORWARD_LIST__HPP

#include "utility.hpp"

#ifndef CTC_IN_A_MODULE
#include <type_traits>
#endif

// A fixed-capacity constexpr singly-linked list (std::forward_list):
// nodes in a plain array linked by INDICES (pointers into your own
// members are not structural-friendly and would dangle on copy).
// Iterators stay valid across other insertions/erasures, which is the
// point of a list.
//
// CAVEAT (the one container family where this is unavoidable): node
// placement depends on the operation history, so equal contents are
// NOT guaranteed to be equivalent template arguments - only == is
// content-based. with_capacity() (and so shrunk<V>()) rebuilds
// compactly and canonically: shrink before using a list as an NTTP if
// identity matters.

namespace ctc {

CTC_EXPORT template <typename T, size_t N> struct forward_list {
	static_assert(std::is_default_constructible_v<T>, "ctc::forward_list<T, N> requires a default-constructible T");

	using value_type = T;
	using size_type = size_t;

	static constexpr size_type nil = static_cast<size_type>(-1);

	struct node {
		T value{};
		size_type next{nil};
	};

	// vacated nodes are reset to T{} and threaded on the free list
	node content[N ? N : 1]{};
	size_type head{nil};
	size_type free_head{0};
	size_type real_size{0};

	constexpr forward_list() noexcept {
		for (size_type i{0}; i < N; ++i) {
			content[i].next = i + 1 < N ? i + 1 : nil;
		}
		if (N == 0) {
			free_head = nil;
		}
	}

	struct const_iterator {
		const forward_list * list{nullptr};
		size_type index{nil};

		constexpr const T & operator*() const noexcept {
			return list->content[index].value;
		}
		constexpr const T * operator->() const noexcept {
			return &list->content[index].value;
		}
		constexpr const_iterator & operator++() noexcept {
			index = list->content[index].next;
			return *this;
		}
		constexpr const_iterator operator++(int) noexcept {
			const_iterator before = *this;
			++*this;
			return before;
		}
		friend constexpr bool operator==(const const_iterator &, const const_iterator &) noexcept = default;
	};
	struct iterator {
		forward_list * list{nullptr};
		size_type index{nil};

		constexpr T & operator*() const noexcept {
			return list->content[index].value;
		}
		constexpr T * operator->() const noexcept {
			return &list->content[index].value;
		}
		constexpr iterator & operator++() noexcept {
			index = list->content[index].next;
			return *this;
		}
		constexpr iterator operator++(int) noexcept {
			iterator before = *this;
			++*this;
			return before;
		}
		constexpr operator const_iterator() const noexcept {
			return {list, index};
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
			detail::precondition_violated("ctc::forward_list: front() on an empty list");
		}
		return content[head].value;
	}
	constexpr const T & front() const noexcept {
		if (real_size == 0) {
			detail::precondition_violated("ctc::forward_list: front() on an empty list");
		}
		return content[head].value;
	}

	// mutation
	constexpr void push_front(const T & value) {
		const size_type index = allocate("ctc::forward_list: push_front on a full list");
		content[index].value = value;
		content[index].next = head;
		head = index;
	}
	constexpr void pop_front() {
		if (real_size == 0) {
			detail::precondition_violated("ctc::forward_list: pop_front on an empty list");
		}
		const size_type index = head;
		head = content[index].next;
		release(index);
	}
	// inserts after `position` (std::forward_list semantics); returns
	// an iterator to the new element
	constexpr iterator insert_after(const_iterator position, const T & value) {
		const size_type index = allocate("ctc::forward_list: insert_after on a full list");
		content[index].value = value;
		content[index].next = content[position.index].next;
		content[position.index].next = index;
		return {this, index};
	}
	// erases the element after `position`; returns an iterator to the
	// one after the erased element
	constexpr iterator erase_after(const_iterator position) {
		const size_type victim = content[position.index].next;
		if (victim == nil) {
			detail::precondition_violated("ctc::forward_list: erase_after with nothing after");
		}
		content[position.index].next = content[victim].next;
		const size_type following = content[victim].next;
		release(victim);
		return {this, following};
	}
	constexpr void clear() {
		while (real_size != 0) {
			pop_front();
		}
	}

	// a compact, canonically-laid-out copy (this is also how a list
	// becomes NTTP-identity-safe)
	template <size_t M> constexpr forward_list<T, M> with_capacity() const {
		if (real_size > M) {
			detail::precondition_violated("ctc::forward_list: content does not fit the new capacity");
		}
		forward_list<T, M> result;
		size_type out{0};
		for (size_type i{head}; i != nil; i = content[i].next, ++out) {
			result.content[out].value = content[i].value;
			result.content[out].next = content[i].next != nil ? out + 1 : forward_list<T, M>::nil;
		}
		result.head = real_size != 0 ? 0 : forward_list<T, M>::nil;
		result.free_head = real_size < M ? real_size : forward_list<T, M>::nil;
		result.real_size = real_size;
		return result;
	}

private:
	constexpr size_type allocate(const char * full_message) {
		if (free_head == nil) {
			detail::precondition_violated(full_message);
		}
		const size_type index = free_head;
		free_head = content[index].next;
		++real_size;
		return index;
	}
	constexpr void release(size_type index) {
		content[index].value = T{};
		content[index].next = free_head;
		free_head = index;
		--real_size;
	}
};

// equality: element-wise in list order, across capacities
CTC_EXPORT template <typename T, size_t A, size_t B> constexpr bool operator==(const forward_list<T, A> & lhs, const forward_list<T, B> & rhs) {
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
