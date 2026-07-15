#ifndef CTC__UNORDERED_MAP__HPP
#define CTC__UNORDERED_MAP__HPP

#include "utility.hpp"
#include "pair.hpp"

#ifndef CTC_IN_A_MODULE
#include <initializer_list>
#include <type_traits>
#endif

// Fixed-capacity constexpr unordered maps (std::unordered_map /
// std::unordered_multimap). At compile-time sizes a hash table buys
// nothing and its probe layout would poison NTTP identity, so the
// implementation is a flat array scanned linearly with KeyEqual - and
// as a documented GUARANTEE (stronger than std), iteration is in
// insertion order: an unordered_map models a JSON object. The Hash
// parameter is accepted for interface compatibility and ignored.
//
// Lookups are heterogeneous: any key type KeyEqual accepts works (a
// std::string_view against ctc::string keys, for instance).

namespace ctc {

CTC_EXPORT template <typename Key, typename Value, size_t N, typename Hash = void, typename KeyEqual = equal_to> struct unordered_map {
	static_assert(std::is_default_constructible_v<Key> && std::is_default_constructible_v<Value>, "ctc::unordered_map<Key, Value, N> requires default-constructible Key and Value");
	static_assert(std::is_empty_v<KeyEqual>, "ctc::unordered_map: KeyEqual must be stateless (it is not stored)");

	using key_type = Key;
	using mapped_type = Value;
	using value_type = pair<Key, Value>;
	using size_type = size_t;
	using hasher = Hash;
	using key_equal = KeyEqual;
	using reference = value_type &;
	using const_reference = const value_type &;
	using iterator = value_type *;
	using const_iterator = const value_type *;

	// every slot past size() is kept equal to value_type{} by the
	// mutators, so equal contents (same insertion order) mean
	// equivalent template arguments when a value is used as an NTTP
	value_type content[N ? N : 1]{};
	size_type real_size{0};

	constexpr unordered_map() noexcept = default;

	constexpr unordered_map(std::initializer_list<value_type> init) {
		for (const value_type & element : init) {
			insert_or_assign(element.first, element.second);
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
	constexpr iterator begin() noexcept {
		return content;
	}
	constexpr const_iterator begin() const noexcept {
		return content;
	}
	constexpr iterator end() noexcept {
		return content + real_size;
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
	constexpr value_type * data() noexcept {
		return content;
	}
	constexpr const value_type * data() const noexcept {
		return content;
	}

	// lookup
	template <typename K> constexpr iterator find(const K & key) noexcept {
		for (size_type i{0}; i < real_size; ++i) {
			if (KeyEqual{}(content[i].first, key)) {
				return content + i;
			}
		}
		return end();
	}
	template <typename K> constexpr const_iterator find(const K & key) const noexcept {
		for (size_type i{0}; i < real_size; ++i) {
			if (KeyEqual{}(content[i].first, key)) {
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
	template <typename K> constexpr Value & at(const K & key) noexcept {
		const iterator position = find(key);
		if (position == end()) {
			detail::precondition_violated("ctc::unordered_map: at() with a key that is not in the map");
		}
		return position->second;
	}
	template <typename K> constexpr const Value & at(const K & key) const noexcept {
		const const_iterator position = find(key);
		if (position == end()) {
			detail::precondition_violated("ctc::unordered_map: at() with a key that is not in the map");
		}
		return position->second;
	}
	// like std::unordered_map, m[key] inserts a default-constructed
	// value for a missing key: m["answer"] = 42;
	template <typename K> constexpr Value & operator[](const K & key) {
		const iterator position = find(key);
		if (position != end()) {
			return position->second;
		}
		if (real_size == N) {
			detail::precondition_violated("ctc::unordered_map: insertion into a full map");
		}
		content[real_size].first = Key{key};
		return content[real_size++].second;
	}

	// insertion (appends: insertion order is the iteration order)
	template <typename K> constexpr pair<iterator, bool> insert_or_assign(const K & key, const Value & value) {
		if (const iterator position = find(key); position != end()) {
			position->second = value;
			return {position, false};
		}
		if (real_size == N) {
			detail::precondition_violated("ctc::unordered_map: insertion into a full map");
		}
		content[real_size] = value_type{Key{key}, value};
		return {content + real_size++, true};
	}
	// like std::unordered_map::insert, an existing key is left untouched
	constexpr pair<iterator, bool> insert(const value_type & element) {
		if (const iterator position = find(element.first); position != end()) {
			return {position, false};
		}
		if (real_size == N) {
			detail::precondition_violated("ctc::unordered_map: insertion into a full map");
		}
		content[real_size] = element;
		return {content + real_size++, true};
	}
	template <typename K> constexpr pair<iterator, bool> try_emplace(const K & key, const Value & value) {
		if (const iterator position = find(key); position != end()) {
			return {position, false};
		}
		if (real_size == N) {
			detail::precondition_violated("ctc::unordered_map: insertion into a full map");
		}
		content[real_size] = value_type{Key{key}, value};
		return {content + real_size++, true};
	}

	// removal (later elements shift down; insertion order is preserved,
	// the vacated slot is reset to value_type{})
	template <typename K> constexpr size_type erase(const K & key) {
		const iterator position = find(key);
		if (position == end()) {
			return 0;
		}
		const size_type index = static_cast<size_type>(position - content);
		for (size_type i{index}; i + 1 < real_size; ++i) {
			content[i] = content[i + 1];
		}
		content[--real_size] = value_type{};
		return 1;
	}
	constexpr void clear() {
		for (size_type i{0}; i < real_size; ++i) {
			content[i] = value_type{};
		}
		real_size = 0;
	}

	// a copy with a different capacity (checked to fit; shrunk<V>()
	// right-sizes to exactly size())
	template <size_t M> constexpr unordered_map<Key, Value, M, Hash, KeyEqual> with_capacity() const {
		if (real_size > M) {
			detail::precondition_violated("ctc::unordered_map: content does not fit the new capacity");
		}
		unordered_map<Key, Value, M, Hash, KeyEqual> result;
		for (size_type i{0}; i < real_size; ++i) {
			result.content[i] = content[i];
		}
		result.real_size = real_size;
		return result;
	}
};

// std::unordered_multimap: duplicate keys allowed. A new duplicate is
// inserted directly after the existing equal keys, so equal keys stay
// ADJACENT (equal_range works) while key groups keep first-insertion
// order.
CTC_EXPORT template <typename Key, typename Value, size_t N, typename Hash = void, typename KeyEqual = equal_to> struct unordered_multimap {
	static_assert(std::is_default_constructible_v<Key> && std::is_default_constructible_v<Value>, "ctc::unordered_multimap<Key, Value, N> requires default-constructible Key and Value");
	static_assert(std::is_empty_v<KeyEqual>, "ctc::unordered_multimap: KeyEqual must be stateless (it is not stored)");

	using key_type = Key;
	using mapped_type = Value;
	using value_type = pair<Key, Value>;
	using size_type = size_t;
	using hasher = Hash;
	using key_equal = KeyEqual;
	using iterator = value_type *;
	using const_iterator = const value_type *;

	value_type content[N ? N : 1]{};
	size_type real_size{0};

	constexpr unordered_multimap() noexcept = default;

	constexpr unordered_multimap(std::initializer_list<value_type> init) {
		for (const value_type & element : init) {
			insert(element);
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

	constexpr iterator begin() noexcept {
		return content;
	}
	constexpr const_iterator begin() const noexcept {
		return content;
	}
	constexpr iterator end() noexcept {
		return content + real_size;
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
			if (KeyEqual{}(content[i].first, key)) {
				return content + i;
			}
		}
		return end();
	}
	template <typename K> constexpr bool contains(const K & key) const noexcept {
		return find(key) != end();
	}
	template <typename K> constexpr size_type count(const K & key) const noexcept {
		const auto [from, upto] = equal_range(key);
		return static_cast<size_type>(upto - from);
	}
	// equal keys are adjacent (see insert), so the range is contiguous
	template <typename K> constexpr pair<const_iterator, const_iterator> equal_range(const K & key) const noexcept {
		const const_iterator from = find(key);
		if (from == end()) {
			return {end(), end()};
		}
		const_iterator upto = from + 1;
		while (upto != end() && KeyEqual{}(upto->first, key)) {
			++upto;
		}
		return {from, upto};
	}

	constexpr iterator insert(const value_type & element) {
		if (real_size == N) {
			detail::precondition_violated("ctc::unordered_multimap: insertion into a full multimap");
		}
		size_type index = real_size;
		if (const const_iterator existing = find(element.first); existing != end()) {
			index = static_cast<size_type>(equal_range(element.first).second - content);
		}
		for (size_type i{real_size}; i > index; --i) {
			content[i] = content[i - 1];
		}
		content[index] = element;
		++real_size;
		return content + index;
	}

	// removal erases the whole equal range, like std::unordered_multimap
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
			content[i] = value_type{};
		}
		real_size -= removed;
		return removed;
	}
	constexpr void clear() {
		for (size_type i{0}; i < real_size; ++i) {
			content[i] = value_type{};
		}
		real_size = 0;
	}

	template <size_t M> constexpr unordered_multimap<Key, Value, M, Hash, KeyEqual> with_capacity() const {
		if (real_size > M) {
			detail::precondition_violated("ctc::unordered_multimap: content does not fit the new capacity");
		}
		unordered_multimap<Key, Value, M, Hash, KeyEqual> result;
		for (size_type i{0}; i < real_size; ++i) {
			result.content[i] = content[i];
		}
		result.real_size = real_size;
		return result;
	}
};

// equality: element-wise in stored order, across capacities (the
// containers are insertion-ordered, and so is their equality)
CTC_EXPORT template <typename Key, typename Value, size_t A, size_t B, typename Hash, typename KeyEqual> constexpr bool operator==(const unordered_map<Key, Value, A, Hash, KeyEqual> & lhs, const unordered_map<Key, Value, B, Hash, KeyEqual> & rhs) {
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
CTC_EXPORT template <typename Key, typename Value, size_t A, size_t B, typename Hash, typename KeyEqual> constexpr bool operator==(const unordered_multimap<Key, Value, A, Hash, KeyEqual> & lhs, const unordered_multimap<Key, Value, B, Hash, KeyEqual> & rhs) {
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
