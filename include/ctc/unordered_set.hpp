#ifndef CTC__UNORDERED_SET__HPP
#define CTC__UNORDERED_SET__HPP

#include "utility.hpp"
#include "pair.hpp"

#ifndef CTC_IN_A_MODULE
#include <initializer_list>
#include <type_traits>
#endif

// Fixed-capacity constexpr unordered sets (std::unordered_set /
// std::unordered_multiset). Same design as unordered_map.hpp: a flat
// array scanned linearly with KeyEqual, iteration in insertion order
// (guaranteed), Hash accepted for interface compatibility and ignored.

namespace ctc {

CTC_EXPORT template <typename Key, size_t N, typename Hash = void, typename KeyEqual = equal_to> struct unordered_set {
	static_assert(std::is_default_constructible_v<Key>, "ctc::unordered_set<Key, N> requires a default-constructible Key");
	static_assert(std::is_empty_v<KeyEqual>, "ctc::unordered_set: KeyEqual must be stateless (it is not stored)");

	using key_type = Key;
	using value_type = Key;
	using size_type = size_t;
	using hasher = Hash;
	using key_equal = KeyEqual;
	using iterator = const Key *; // set elements are immutable, like std
	using const_iterator = const Key *;

	// every slot past size() is kept equal to Key{} by the mutators
	Key content[N ? N : 1]{};
	size_type real_size{0};

	constexpr unordered_set() noexcept = default;

	constexpr unordered_set(std::initializer_list<Key> init) {
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

	// iteration, in insertion order (guaranteed)
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
	template <typename K> constexpr const_iterator find(const K & key) const noexcept {
		for (size_type i{0}; i < real_size; ++i) {
			if (KeyEqual{}(content[i], key)) {
				return content + i;
			}
		}
		return end();
	}
	template <typename K> constexpr bool contains(const K & key) const noexcept {
		return find(key) != end();
	}
	template <typename K> constexpr size_type count(const K & key) const noexcept {
		return find(key) != end() ? 1 : 0;
	}

	// insertion (appends; an existing key is left untouched)
	constexpr pair<const_iterator, bool> insert(const Key & key) {
		if (const const_iterator position = find(key); position != end()) {
			return {position, false};
		}
		if (real_size == N) {
			detail::precondition_violated("ctc::unordered_set: insertion into a full set");
		}
		content[real_size] = key;
		return {content + real_size++, true};
	}

	// removal (later elements shift down, the vacated slot is reset)
	template <typename K> constexpr size_type erase(const K & key) {
		const const_iterator position = find(key);
		if (position == end()) {
			return 0;
		}
		const size_type index = static_cast<size_type>(position - content);
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
	template <size_t M> constexpr unordered_set<Key, M, Hash, KeyEqual> with_capacity() const {
		if (real_size > M) {
			detail::precondition_violated("ctc::unordered_set: content does not fit the new capacity");
		}
		unordered_set<Key, M, Hash, KeyEqual> result;
		for (size_type i{0}; i < real_size; ++i) {
			result.content[i] = content[i];
		}
		result.real_size = real_size;
		return result;
	}
};

// std::unordered_multiset: duplicates allowed and kept ADJACENT to
// their equals (equal_range works); groups keep first-insertion order.
CTC_EXPORT template <typename Key, size_t N, typename Hash = void, typename KeyEqual = equal_to> struct unordered_multiset {
	static_assert(std::is_default_constructible_v<Key>, "ctc::unordered_multiset<Key, N> requires a default-constructible Key");
	static_assert(std::is_empty_v<KeyEqual>, "ctc::unordered_multiset: KeyEqual must be stateless (it is not stored)");

	using key_type = Key;
	using value_type = Key;
	using size_type = size_t;
	using hasher = Hash;
	using key_equal = KeyEqual;
	using iterator = const Key *;
	using const_iterator = const Key *;

	Key content[N ? N : 1]{};
	size_type real_size{0};

	constexpr unordered_multiset() noexcept = default;

	constexpr unordered_multiset(std::initializer_list<Key> init) {
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

	template <typename K> constexpr const_iterator find(const K & key) const noexcept {
		for (size_type i{0}; i < real_size; ++i) {
			if (KeyEqual{}(content[i], key)) {
				return content + i;
			}
		}
		return end();
	}
	template <typename K> constexpr bool contains(const K & key) const noexcept {
		return find(key) != end();
	}
	template <typename K> constexpr pair<const_iterator, const_iterator> equal_range(const K & key) const noexcept {
		const const_iterator from = find(key);
		if (from == end()) {
			return {end(), end()};
		}
		const_iterator upto = from + 1;
		while (upto != end() && KeyEqual{}(*upto, key)) {
			++upto;
		}
		return {from, upto};
	}
	template <typename K> constexpr size_type count(const K & key) const noexcept {
		const auto [from, upto] = equal_range(key);
		return static_cast<size_type>(upto - from);
	}

	constexpr const_iterator insert(const Key & key) {
		if (real_size == N) {
			detail::precondition_violated("ctc::unordered_multiset: insertion into a full multiset");
		}
		size_type index = real_size;
		if (find(key) != end()) {
			index = static_cast<size_type>(equal_range(key).second - content);
		}
		for (size_type i{real_size}; i > index; --i) {
			content[i] = content[i - 1];
		}
		content[index] = key;
		++real_size;
		return content + index;
	}

	// removal erases the whole equal range
	template <typename K> constexpr size_type erase(const K & key) {
		const auto [from, upto] = equal_range(key);
		const size_type removed = static_cast<size_type>(upto - from);
		if (removed == 0) {
			return 0;
		}
		const size_type index = static_cast<size_type>(from - content);
		for (size_type i{index}; i + removed < real_size; ++i) {
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

	template <size_t M> constexpr unordered_multiset<Key, M, Hash, KeyEqual> with_capacity() const {
		if (real_size > M) {
			detail::precondition_violated("ctc::unordered_multiset: content does not fit the new capacity");
		}
		unordered_multiset<Key, M, Hash, KeyEqual> result;
		for (size_type i{0}; i < real_size; ++i) {
			result.content[i] = content[i];
		}
		result.real_size = real_size;
		return result;
	}
};

// equality: element-wise in stored order, across capacities
CTC_EXPORT template <typename Key, size_t A, size_t B, typename Hash, typename KeyEqual> constexpr bool operator==(const unordered_set<Key, A, Hash, KeyEqual> & lhs, const unordered_set<Key, B, Hash, KeyEqual> & rhs) {
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
CTC_EXPORT template <typename Key, size_t A, size_t B, typename Hash, typename KeyEqual> constexpr bool operator==(const unordered_multiset<Key, A, Hash, KeyEqual> & lhs, const unordered_multiset<Key, B, Hash, KeyEqual> & rhs) {
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
