#ifndef CTC__VECTOR__HPP
#define CTC__VECTOR__HPP

#include "utility.hpp"

#ifndef CTC_IN_A_MODULE
#include <initializer_list>
#include <type_traits>
#endif

// A fixed-capacity constexpr vector (an inplace_vector that stays a
// structural type). The elements live in a plain public array, so a
// vector built in constant evaluation can be a non-type template
// parameter and can persist to runtime in static storage.

namespace ctc {

CTC_EXPORT template <typename T, size_t N> struct vector {
	// the whole array exists up front, so elements must themselves be
	// default-constructible (the price of staying a structural type -
	// union tricks would lose NTTP support)
	static_assert(std::is_default_constructible_v<T>, "ctc::vector<T, N> requires a default-constructible T");

	using value_type = T;
	using size_type = size_t;
	using reference = T &;
	using const_reference = const T &;
	using iterator = T *;
	using const_iterator = const T *;

	// every slot past size() is kept equal to T{} by the mutators, so
	// equal contents mean equivalent template arguments when a value is
	// used as an NTTP (template-argument equivalence compares every
	// array element, not just the first size() elements).
	T content[N ? N : 1]{};
	size_type real_size{0};

	constexpr vector() noexcept = default;

	constexpr vector(std::initializer_list<T> init) {
		if (init.size() > N) {
			detail::precondition_violated("ctc::vector: content does not fit the capacity");
		}
		for (const T & element : init) {
			content[real_size++] = element;
		}
	}

	constexpr vector(size_type count, const T & value) {
		if (count > N) {
			detail::precondition_violated("ctc::vector: content does not fit the capacity");
		}
		for (size_type i{0}; i < count; ++i) {
			content[i] = value;
		}
		real_size = count;
	}

	constexpr vector(const T * from, size_type count) {
		if (count > N) {
			detail::precondition_violated("ctc::vector: content does not fit the capacity");
		}
		for (size_type i{0}; i < count; ++i) {
			content[i] = from[i];
		}
		real_size = count;
	}

	// from another capacity (checked to fit)
	template <size_t M> requires (M != N) explicit constexpr vector(const vector<T, M> & other): vector{other.data(), other.size()} { }

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

	// element access
	constexpr T * data() noexcept {
		return content;
	}
	constexpr const T * data() const noexcept {
		return content;
	}
	constexpr reference operator[](size_type i) noexcept {
		if (i >= real_size) {
			detail::precondition_violated("ctc::vector: index out of range");
		}
		return content[i];
	}
	constexpr const_reference operator[](size_type i) const noexcept {
		if (i >= real_size) {
			detail::precondition_violated("ctc::vector: index out of range");
		}
		return content[i];
	}
	constexpr reference at(size_type i) noexcept {
		return (*this)[i];
	}
	constexpr const_reference at(size_type i) const noexcept {
		return (*this)[i];
	}
	constexpr reference front() noexcept {
		return (*this)[0];
	}
	constexpr const_reference front() const noexcept {
		return (*this)[0];
	}
	constexpr reference back() noexcept {
		if (real_size == 0) {
			detail::precondition_violated("ctc::vector: back() on an empty vector");
		}
		return content[real_size - 1];
	}
	constexpr const_reference back() const noexcept {
		if (real_size == 0) {
			detail::precondition_violated("ctc::vector: back() on an empty vector");
		}
		return content[real_size - 1];
	}

	// iteration
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

	// mutation (every vacated slot is reset to T{}, see `content`)
	constexpr void push_back(const T & value) {
		if (real_size == N) {
			detail::precondition_violated("ctc::vector: push_back on a full vector");
		}
		content[real_size++] = value;
	}
	template <typename... Args> constexpr reference emplace_back(Args &&... args) {
		if (real_size == N) {
			detail::precondition_violated("ctc::vector: emplace_back on a full vector");
		}
		content[real_size] = T{static_cast<Args &&>(args)...};
		return content[real_size++];
	}
	constexpr void pop_back() {
		if (real_size == 0) {
			detail::precondition_violated("ctc::vector: pop_back on an empty vector");
		}
		content[--real_size] = T{};
	}
	constexpr void clear() {
		for (size_type i{0}; i < real_size; ++i) {
			content[i] = T{};
		}
		real_size = 0;
	}
	constexpr void resize(size_type count, const T & value = T{}) {
		if (count > N) {
			detail::precondition_violated("ctc::vector: resize beyond the capacity");
		}
		for (size_type i{real_size}; i < count; ++i) {
			content[i] = value;
		}
		for (size_type i{count}; i < real_size; ++i) {
			content[i] = T{};
		}
		real_size = count;
	}
	constexpr iterator insert(const_iterator position, const T & value) {
		if (real_size == N) {
			detail::precondition_violated("ctc::vector: insert into a full vector");
		}
		const size_type index = static_cast<size_type>(position - content);
		for (size_type i{real_size}; i > index; --i) {
			content[i] = content[i - 1];
		}
		content[index] = value;
		++real_size;
		return content + index;
	}
	constexpr iterator erase(const_iterator position) {
		const size_type index = static_cast<size_type>(position - content);
		if (index >= real_size) {
			detail::precondition_violated("ctc::vector: erase position out of range");
		}
		for (size_type i{index}; i + 1 < real_size; ++i) {
			content[i] = content[i + 1];
		}
		content[--real_size] = T{};
		return content + index;
	}

	// a copy with a different capacity (checked to fit; shrunk<V>()
	// right-sizes to exactly size())
	template <size_t M> constexpr vector<T, M> with_capacity() const {
		if (real_size > M) {
			detail::precondition_violated("ctc::vector: content does not fit the new capacity");
		}
		vector<T, M> result;
		for (size_type i{0}; i < real_size; ++i) {
			result.content[i] = content[i];
		}
		result.real_size = real_size;
		return result;
	}
};

// equality: element-wise, across capacities
CTC_EXPORT template <typename T, size_t A, size_t B> constexpr bool operator==(const vector<T, A> & lhs, const vector<T, B> & rhs) {
	if (lhs.size() != rhs.size()) {
		return false;
	}
	for (size_t i{0}; i < lhs.size(); ++i) {
		if (!(lhs[i] == rhs[i])) {
			return false;
		}
	}
	return true;
}

} // namespace ctc

#endif
