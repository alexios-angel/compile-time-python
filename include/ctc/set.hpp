#ifndef CTC__SET__HPP
#define CTC__SET__HPP

#include "utility.hpp"
#include "pair.hpp"

#ifndef CTC_IN_A_MODULE
#include <initializer_list>
#include <type_traits>
#endif

// Fixed-capacity constexpr sorted sets (std::set / std::multiset).
// A plain public array kept sorted by Compare - binary-search lookups,
// heterogeneous (any key comparable through the transparent Compare).
// Because the layout is canonical for a set (sorted, unique), equal
// contents mean equivalent template arguments no matter the insertion
// order. Compare must be stateless: it is constructed on use, not
// stored. Elements are immutable through iterators, like std.

namespace ctc {

namespace detail {

struct key_is_the_element {
	template <typename T> constexpr const T & operator()(const T & element) const noexcept {
		return element;
	}
};

} // namespace detail

CTC_EXPORT template <typename Key, size_t N, typename Compare = less> struct set {
	static_assert(std::is_default_constructible_v<Key>, "ctc::set<Key, N> requires a default-constructible Key");
	static_assert(std::is_empty_v<Compare>, "ctc::set: Compare must be stateless (it is not stored)");

	using key_type = Key;
	using value_type = Key;
	using size_type = size_t;
	using key_compare = Compare;
	using iterator = const Key *; // set elements are immutable, like std
	using const_iterator = const Key *;

	// every slot past size() is kept equal to Key{} by the mutators
	Key content[N ? N : 1]{};
	size_type real_size{0};

	constexpr set() noexcept = default;

	constexpr set(std::initializer_list<Key> init) {
		for (const Key & key : init) {
			insert(key);
		}
	}

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

	// iteration, in sorted order
	constexpr const_iterator begin() const noexcept {
		return content;
	}
	constexpr const_iterator end() const noexcept {
		return content + real_size;
	}
	constexpr const_iterator cbegin() const noexcept {
		return begin();
	}
	constexpr const_iterator cend() const noexcept {
		return end();
	}
	constexpr const Key * data() const noexcept {
		return content;
	}

	// lookup
	template <typename K> constexpr const_iterator lower_bound(const K & key) const noexcept {
		return content + detail::lower_bound_index<Compare>(content, real_size, key, detail::key_is_the_element{});
	}
	template <typename K> constexpr const_iterator upper_bound(const K & key) const noexcept {
		return content + detail::upper_bound_index<Compare>(content, real_size, key, detail::key_is_the_element{});
	}
	template <typename K> constexpr pair<const_iterator, const_iterator> equal_range(const K & key) const noexcept {
		return {lower_bound(key), upper_bound(key)};
	}
	template <typename K> constexpr const_iterator find(const K & key) const noexcept {
		const const_iterator position = lower_bound(key);
		if (position != end() && !Compare{}(key, *position)) {
			return position;
		}
		return end();
	}
	template <typename K> constexpr bool contains(const K & key) const noexcept {
		return find(key) != end();
	}
	template <typename K> constexpr size_type count(const K & key) const noexcept {
		return static_cast<size_type>(upper_bound(key) - lower_bound(key));
	}

	// insertion (an existing key is left untouched, like std::set)
	constexpr pair<const_iterator, bool> insert(const Key & key) {
		const size_type index = detail::lower_bound_index<Compare>(content, real_size, key, detail::key_is_the_element{});
		if (index < real_size && !Compare{}(key, content[index])) {
			return {content + index, false};
		}
		if (real_size == N) {
			detail::precondition_violated("ctc::set: insertion into a full set");
		}
		for (size_type i{real_size}; i > index; --i) {
			content[i] = content[i - 1];
		}
		content[index] = key;
		++real_size;
		return {content + index, true};
	}

	// removal (the vacated slot is reset to Key{})
	template <typename K> constexpr size_type erase(const K & key) {
		const size_type index = detail::lower_bound_index<Compare>(content, real_size, key, detail::key_is_the_element{});
		if (index == real_size || Compare{}(key, content[index])) {
			return 0;
		}
		for (size_type i{index}; i + 1 < real_size; ++i) {
			content[i] = content[i + 1];
		}
		content[--real_size] = Key{};
		return 1;
	}
	constexpr void clear() {
		for (size_type i{0}; i < real_size; ++i) {
			content[i] = Key{};
		}
		real_size = 0;
	}

	// a copy with a different capacity (checked to fit)
	template <size_t M> constexpr set<Key, M, Compare> with_capacity() const {
		if (real_size > M) {
			detail::precondition_violated("ctc::set: content does not fit the new capacity");
		}
		set<Key, M, Compare> result;
		for (size_type i{0}; i < real_size; ++i) {
			result.content[i] = content[i];
		}
		result.real_size = real_size;
		return result;
	}
};

// std::multiset: duplicate keys allowed; a new equal key is placed
// after the existing ones (stable), so the layout is deterministic
// for a given insertion sequence.
CTC_EXPORT template <typename Key, size_t N, typename Compare = less> struct multiset {
	static_assert(std::is_default_constructible_v<Key>, "ctc::multiset<Key, N> requires a default-constructible Key");
	static_assert(std::is_empty_v<Compare>, "ctc::multiset: Compare must be stateless (it is not stored)");

	using key_type = Key;
	using value_type = Key;
	using size_type = size_t;
	using key_compare = Compare;
	using iterator = const Key *;
	using const_iterator = const Key *;

	Key content[N ? N : 1]{};
	size_type real_size{0};

	constexpr multiset() noexcept = default;

	constexpr multiset(std::initializer_list<Key> init) {
		for (const Key & key : init) {
			insert(key);
		}
	}

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

	constexpr const_iterator begin() const noexcept {
		return content;
	}
	constexpr const_iterator end() const noexcept {
		return content + real_size;
	}
	constexpr const_iterator cbegin() const noexcept {
		return begin();
	}
	constexpr const_iterator cend() const noexcept {
		return end();
	}
	constexpr const Key * data() const noexcept {
		return content;
	}

	template <typename K> constexpr const_iterator lower_bound(const K & key) const noexcept {
		return content + detail::lower_bound_index<Compare>(content, real_size, key, detail::key_is_the_element{});
	}
	template <typename K> constexpr const_iterator upper_bound(const K & key) const noexcept {
		return content + detail::upper_bound_index<Compare>(content, real_size, key, detail::key_is_the_element{});
	}
	template <typename K> constexpr pair<const_iterator, const_iterator> equal_range(const K & key) const noexcept {
		return {lower_bound(key), upper_bound(key)};
	}
	template <typename K> constexpr const_iterator find(const K & key) const noexcept {
		const const_iterator position = lower_bound(key);
		if (position != end() && !Compare{}(key, *position)) {
			return position;
		}
		return end();
	}
	template <typename K> constexpr bool contains(const K & key) const noexcept {
		return find(key) != end();
	}
	template <typename K> constexpr size_type count(const K & key) const noexcept {
		return static_cast<size_type>(upper_bound(key) - lower_bound(key));
	}

	constexpr const_iterator insert(const Key & key) {
		if (real_size == N) {
			detail::precondition_violated("ctc::multiset: insertion into a full multiset");
		}
		const size_type index = detail::upper_bound_index<Compare>(content, real_size, key, detail::key_is_the_element{});
		for (size_type i{real_size}; i > index; --i) {
			content[i] = content[i - 1];
		}
		content[index] = key;
		++real_size;
		return content + index;
	}

	// removal erases the whole equal range, like std::multiset
	template <typename K> constexpr size_type erase(const K & key) {
		const size_type from = detail::lower_bound_index<Compare>(content, real_size, key, detail::key_is_the_element{});
		const size_type upto = detail::upper_bound_index<Compare>(content, real_size, key, detail::key_is_the_element{});
		const size_type removed = upto - from;
		for (size_type i{from}; i + removed < real_size; ++i) {
			content[i] = content[i + removed];
		}
		for (size_type i{real_size - removed}; i < real_size; ++i) {
			content[i] = Key{};
		}
		real_size -= removed;
		return removed;
	}
	constexpr void clear() {
		for (size_type i{0}; i < real_size; ++i) {
			content[i] = Key{};
		}
		real_size = 0;
	}

	template <size_t M> constexpr multiset<Key, M, Compare> with_capacity() const {
		if (real_size > M) {
			detail::precondition_violated("ctc::multiset: content does not fit the new capacity");
		}
		multiset<Key, M, Compare> result;
		for (size_type i{0}; i < real_size; ++i) {
			result.content[i] = content[i];
		}
		result.real_size = real_size;
		return result;
	}
};

// equality: element-wise in sorted order, across capacities
CTC_EXPORT template <typename Key, size_t A, size_t B, typename Compare> constexpr bool operator==(const set<Key, A, Compare> & lhs, const set<Key, B, Compare> & rhs) {
	if (lhs.size() != rhs.size()) {
		return false;
	}
	for (size_t i{0}; i < lhs.size(); ++i) {
		if (!(lhs.content[i] == rhs.content[i])) {
			return false;
		}
	}
	return true;
}
CTC_EXPORT template <typename Key, size_t A, size_t B, typename Compare> constexpr bool operator==(const multiset<Key, A, Compare> & lhs, const multiset<Key, B, Compare> & rhs) {
	if (lhs.size() != rhs.size()) {
		return false;
	}
	for (size_t i{0}; i < lhs.size(); ++i) {
		if (!(lhs.content[i] == rhs.content[i])) {
			return false;
		}
	}
	return true;
}

} // namespace ctc

#endif
