#ifndef CTC__MAP__HPP
#define CTC__MAP__HPP

#include "utility.hpp"
#include "pair.hpp"

#ifndef CTC_IN_A_MODULE
#include <initializer_list>
#include <type_traits>
#endif

// Fixed-capacity constexpr sorted maps (std::map / std::multimap).
// A plain public array of ctc::pair kept sorted by Compare on the key:
// binary-search lookups, heterogeneous (any key comparable through the
// transparent Compare). Because the layout of a map is canonical
// (sorted, unique keys), equal contents mean equivalent template
// arguments no matter the insertion order. Compare must be stateless:
// it is constructed on use, not stored.
//
// For the insertion-ordered map (a JSON object), see unordered_map.hpp.

namespace ctc {

namespace detail {

struct key_is_first {
	template <typename P> constexpr const auto & operator()(const P & element) const noexcept {
		return element.first;
	}
};

} // namespace detail

CTC_EXPORT template <typename Key, typename Value, size_t N, typename Compare = less> struct map {
	static_assert(std::is_default_constructible_v<Key> && std::is_default_constructible_v<Value>, "ctc::map<Key, Value, N> requires default-constructible Key and Value");
	static_assert(std::is_empty_v<Compare>, "ctc::map: Compare must be stateless (it is not stored)");

	using key_type = Key;
	using mapped_type = Value;
	using value_type = pair<Key, Value>;
	using size_type = size_t;
	using key_compare = Compare;
	using reference = value_type &;
	using const_reference = const value_type &;
	using iterator = value_type *;
	using const_iterator = const value_type *;

	// every slot past size() is kept equal to value_type{} by the
	// mutators; do not write through an iterator's .first (sortedness
	// and NTTP identity depend on it)
	value_type content[N ? N : 1]{};
	size_type real_size{0};

	constexpr map() noexcept = default;

	constexpr map(std::initializer_list<value_type> init) {
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

	// iteration, in sorted key order
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
	template <typename K> constexpr iterator lower_bound(const K & key) noexcept {
		return content + detail::lower_bound_index<Compare>(content, real_size, key, detail::key_is_first{});
	}
	template <typename K> constexpr const_iterator lower_bound(const K & key) const noexcept {
		return content + detail::lower_bound_index<Compare>(content, real_size, key, detail::key_is_first{});
	}
	template <typename K> constexpr iterator upper_bound(const K & key) noexcept {
		return content + detail::upper_bound_index<Compare>(content, real_size, key, detail::key_is_first{});
	}
	template <typename K> constexpr const_iterator upper_bound(const K & key) const noexcept {
		return content + detail::upper_bound_index<Compare>(content, real_size, key, detail::key_is_first{});
	}
	template <typename K> constexpr iterator find(const K & key) noexcept {
		const iterator position = lower_bound(key);
		if (position != end() && !Compare{}(key, position->first)) {
			return position;
		}
		return end();
	}
	template <typename K> constexpr const_iterator find(const K & key) const noexcept {
		const const_iterator position = lower_bound(key);
		if (position != end() && !Compare{}(key, position->first)) {
			return position;
		}
		return end();
	}
	template <typename K> constexpr bool contains(const K & key) const noexcept {
		return find(key) != end();
	}
	template <typename K> constexpr size_type count(const K & key) const noexcept {
		return find(key) != end() ? 1 : 0;
	}
	template <typename K> constexpr pair<const_iterator, const_iterator> equal_range(const K & key) const noexcept {
		return {lower_bound(key), upper_bound(key)};
	}
	template <typename K> constexpr Value & at(const K & key) noexcept {
		const iterator position = find(key);
		if (position == end()) {
			detail::precondition_violated("ctc::map: at() with a key that is not in the map");
		}
		return position->second;
	}
	template <typename K> constexpr const Value & at(const K & key) const noexcept {
		const const_iterator position = find(key);
		if (position == end()) {
			detail::precondition_violated("ctc::map: at() with a key that is not in the map");
		}
		return position->second;
	}
	// like std::map, m[key] inserts a default-constructed value for a
	// missing key: m["answer"] = 42;
	template <typename K> constexpr Value & operator[](const K & key) {
		return try_emplace_impl(key)->second;
	}

	// insertion
	template <typename K> constexpr pair<iterator, bool> insert_or_assign(const K & key, const Value & value) {
		const size_type before = real_size;
		const iterator position = try_emplace_impl(key);
		const bool inserted = real_size != before;
		position->second = value;
		return {position, inserted};
	}
	// like std::map::insert, an existing key is left untouched
	constexpr pair<iterator, bool> insert(const value_type & element) {
		const size_type before = real_size;
		const iterator position = try_emplace_impl(element.first);
		const bool inserted = real_size != before;
		if (inserted) {
			position->second = element.second;
		}
		return {position, inserted};
	}
	template <typename K> constexpr pair<iterator, bool> try_emplace(const K & key, const Value & value) {
		const size_type before = real_size;
		const iterator position = try_emplace_impl(key);
		const bool inserted = real_size != before;
		if (inserted) {
			position->second = value;
		}
		return {position, inserted};
	}

	// removal (slots shift down; the vacated slot is reset)
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

	// a copy with a different capacity (checked to fit)
	template <size_t M> constexpr map<Key, Value, M, Compare> with_capacity() const {
		if (real_size > M) {
			detail::precondition_violated("ctc::map: content does not fit the new capacity");
		}
		map<Key, Value, M, Compare> result;
		for (size_type i{0}; i < real_size; ++i) {
			result.content[i] = content[i];
		}
		result.real_size = real_size;
		return result;
	}

private:
	// the position of `key`, inserting {Key{key}, Value{}} at the
	// sorted position if missing
	template <typename K> constexpr iterator try_emplace_impl(const K & key) {
		const size_type index = detail::lower_bound_index<Compare>(content, real_size, key, detail::key_is_first{});
		if (index < real_size && !Compare{}(key, content[index].first)) {
			return content + index;
		}
		if (real_size == N) {
			detail::precondition_violated("ctc::map: insertion into a full map");
		}
		for (size_type i{real_size}; i > index; --i) {
			content[i] = content[i - 1];
		}
		content[index] = value_type{Key{key}, Value{}};
		++real_size;
		return content + index;
	}
};

// std::multimap: duplicate keys allowed; a new equal key is placed
// after the existing ones (stable). No operator[] / at / insert_or_assign.
CTC_EXPORT template <typename Key, typename Value, size_t N, typename Compare = less> struct multimap {
	static_assert(std::is_default_constructible_v<Key> && std::is_default_constructible_v<Value>, "ctc::multimap<Key, Value, N> requires default-constructible Key and Value");
	static_assert(std::is_empty_v<Compare>, "ctc::multimap: Compare must be stateless (it is not stored)");

	using key_type = Key;
	using mapped_type = Value;
	using value_type = pair<Key, Value>;
	using size_type = size_t;
	using key_compare = Compare;
	using iterator = value_type *;
	using const_iterator = const value_type *;

	value_type content[N ? N : 1]{};
	size_type real_size{0};

	constexpr multimap() noexcept = default;

	constexpr multimap(std::initializer_list<value_type> init) {
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

	template <typename K> constexpr const_iterator lower_bound(const K & key) const noexcept {
		return content + detail::lower_bound_index<Compare>(content, real_size, key, detail::key_is_first{});
	}
	template <typename K> constexpr const_iterator upper_bound(const K & key) const noexcept {
		return content + detail::upper_bound_index<Compare>(content, real_size, key, detail::key_is_first{});
	}
	template <typename K> constexpr pair<const_iterator, const_iterator> equal_range(const K & key) const noexcept {
		return {lower_bound(key), upper_bound(key)};
	}
	template <typename K> constexpr const_iterator find(const K & key) const noexcept {
		const const_iterator position = lower_bound(key);
		if (position != end() && !Compare{}(key, position->first)) {
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

	constexpr iterator insert(const value_type & element) {
		if (real_size == N) {
			detail::precondition_violated("ctc::multimap: insertion into a full multimap");
		}
		const size_type index = detail::upper_bound_index<Compare>(content, real_size, element.first, detail::key_is_first{});
		for (size_type i{real_size}; i > index; --i) {
			content[i] = content[i - 1];
		}
		content[index] = element;
		++real_size;
		return content + index;
	}

	// removal erases the whole equal range, like std::multimap
	template <typename K> constexpr size_type erase(const K & key) {
		const size_type from = detail::lower_bound_index<Compare>(content, real_size, key, detail::key_is_first{});
		const size_type upto = detail::upper_bound_index<Compare>(content, real_size, key, detail::key_is_first{});
		const size_type removed = upto - from;
		if (removed == 0) {
			return 0;
		}
		for (size_type i{from}; i + removed < real_size; ++i) {
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

	template <size_t M> constexpr multimap<Key, Value, M, Compare> with_capacity() const {
		if (real_size > M) {
			detail::precondition_violated("ctc::multimap: content does not fit the new capacity");
		}
		multimap<Key, Value, M, Compare> result;
		for (size_type i{0}; i < real_size; ++i) {
			result.content[i] = content[i];
		}
		result.real_size = real_size;
		return result;
	}
};

// equality: element-wise in sorted order, across capacities
CTC_EXPORT template <typename Key, typename Value, size_t A, size_t B, typename Compare> constexpr bool operator==(const map<Key, Value, A, Compare> & lhs, const map<Key, Value, B, Compare> & rhs) {
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
CTC_EXPORT template <typename Key, typename Value, size_t A, size_t B, typename Compare> constexpr bool operator==(const multimap<Key, Value, A, Compare> & lhs, const multimap<Key, Value, B, Compare> & rhs) {
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
