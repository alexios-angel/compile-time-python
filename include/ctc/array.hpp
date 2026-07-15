#ifndef CTC__ARRAY__HPP
#define CTC__ARRAY__HPP

#include "utility.hpp"

// A structural std::array: exactly N elements, aggregate (brace
// elision works: ctc::array<int, 3>{1, 2, 3}), guaranteed usable as a
// non-type template parameter. std::array is usually structural too -
// this one is structural BY CONTRACT, like the rest of the library.

namespace ctc {

CTC_EXPORT template <typename T, size_t N> struct array {
	using value_type = T;
	using size_type = size_t;
	using reference = T &;
	using const_reference = const T &;
	using iterator = T *;
	using const_iterator = const T *;

	T content[N ? N : 1]{};

	// observers
	static constexpr size_type size() noexcept {
		return N;
	}
	static constexpr size_type max_size() noexcept {
		return N;
	}
	static constexpr bool empty() noexcept {
		return N == 0;
	}

	// element access
	constexpr T * data() noexcept {
		return content;
	}
	constexpr const T * data() const noexcept {
		return content;
	}
	constexpr reference operator[](size_type i) noexcept {
		if (i >= N) {
			detail::precondition_violated("ctc::array: index out of range");
		}
		return content[i];
	}
	constexpr const_reference operator[](size_type i) const noexcept {
		if (i >= N) {
			detail::precondition_violated("ctc::array: index out of range");
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
		return (*this)[N - 1];
	}
	constexpr const_reference back() const noexcept {
		return (*this)[N - 1];
	}

	// iteration
	constexpr iterator begin() noexcept {
		return content;
	}
	constexpr const_iterator begin() const noexcept {
		return content;
	}
	constexpr iterator end() noexcept {
		return content + N;
	}
	constexpr const_iterator end() const noexcept {
		return content + N;
	}
	constexpr const_iterator cbegin() const noexcept {
		return begin();
	}
	constexpr const_iterator cend() const noexcept {
		return end();
	}

	constexpr void fill(const T & value) {
		for (size_type i{0}; i < N; ++i) {
			content[i] = value;
		}
	}

	friend constexpr bool operator==(const array &, const array &) = default;
};

template <typename T, typename... U> array(T, U...) -> array<T, 1 + sizeof...(U)>;

} // namespace ctc

#endif
