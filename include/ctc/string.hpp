#ifndef CTC__STRING__HPP
#define CTC__STRING__HPP

#include "utility.hpp"

#ifndef CTC_IN_A_MODULE
#include <string_view>
#include <type_traits>
#include <iosfwd>
#endif

// A fixed-capacity constexpr string. Unlike ctll::fixed_string (which
// decodes to UTF-32 code points), the content is CharT code units,
// stored as written - so `string<N>` is byte-oriented and converts to
// std::string_view for free.

namespace ctc {

CTC_EXPORT template <typename CharT, size_t N> struct basic_string {
	using value_type = CharT;
	using size_type = size_t;
	using view_type = std::basic_string_view<CharT>;
	using reference = CharT &;
	using const_reference = const CharT &;
	using iterator = CharT *;
	using const_iterator = const CharT *;

	static constexpr size_type npos = static_cast<size_type>(-1);

	// content[size()] is always CharT{} (so c_str() works), and every
	// unit past the end is kept CharT{} by the mutators - so equal
	// contents mean equivalent template arguments when a value is used
	// as an NTTP (template-argument equivalence compares every array
	// element, not just the first size() units).
	CharT content[N + 1]{};
	size_type real_size{0};

	constexpr basic_string() noexcept = default;

	// from a string literal (the terminator is not copied, not counted)
	template <size_t M> requires (M <= N + 1) constexpr basic_string(const CharT (&literal)[M]) noexcept {
		for (size_type i{0}; i + 1 < M; ++i) {
			content[i] = literal[i];
		}
		real_size = M - 1;
	}

	constexpr basic_string(const CharT * from, size_type count) noexcept {
		if (count > N) {
			detail::precondition_violated("ctc::basic_string: content does not fit the capacity");
		}
		for (size_type i{0}; i < count; ++i) {
			content[i] = from[i];
		}
		real_size = count;
	}

	explicit constexpr basic_string(view_type view) noexcept: basic_string{view.data(), view.size()} { }

	constexpr basic_string(size_type count, CharT unit) noexcept {
		if (count > N) {
			detail::precondition_violated("ctc::basic_string: content does not fit the capacity");
		}
		for (size_type i{0}; i < count; ++i) {
			content[i] = unit;
		}
		real_size = count;
	}

	// from another capacity (checked to fit)
	template <size_t M> requires (M != N) explicit constexpr basic_string(const basic_string<CharT, M> & other) noexcept: basic_string{other.data(), other.size()} { }

	// observers
	constexpr size_type size() const noexcept {
		return real_size;
	}
	constexpr size_type length() const noexcept {
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
	constexpr CharT * data() noexcept {
		return content;
	}
	constexpr const CharT * data() const noexcept {
		return content;
	}
	constexpr const CharT * c_str() const noexcept {
		return content;
	}
	constexpr reference operator[](size_type i) noexcept {
		if (i >= real_size) {
			detail::precondition_violated("ctc::basic_string: index out of range");
		}
		return content[i];
	}
	constexpr const_reference operator[](size_type i) const noexcept {
		if (i >= real_size) {
			detail::precondition_violated("ctc::basic_string: index out of range");
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
			detail::precondition_violated("ctc::basic_string: back() on an empty string");
		}
		return content[real_size - 1];
	}
	constexpr const_reference back() const noexcept {
		if (real_size == 0) {
			detail::precondition_violated("ctc::basic_string: back() on an empty string");
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

	// conversion
	constexpr view_type view() const noexcept {
		return view_type{content, real_size};
	}
	constexpr operator view_type() const noexcept {
		return view();
	}

	// mutation (every vacated unit is reset to CharT{}, see `content`)
	constexpr void push_back(CharT unit) noexcept {
		if (real_size == N) {
			detail::precondition_violated("ctc::basic_string: push_back on a full string");
		}
		content[real_size++] = unit;
	}
	constexpr void pop_back() noexcept {
		if (real_size == 0) {
			detail::precondition_violated("ctc::basic_string: pop_back on an empty string");
		}
		content[--real_size] = CharT{};
	}
	constexpr void clear() noexcept {
		for (size_type i{0}; i < real_size; ++i) {
			content[i] = CharT{};
		}
		real_size = 0;
	}
	constexpr void resize(size_type count, CharT unit = CharT{}) noexcept {
		if (count > N) {
			detail::precondition_violated("ctc::basic_string: resize beyond the capacity");
		}
		for (size_type i{real_size}; i < count; ++i) {
			content[i] = unit;
		}
		for (size_type i{count}; i < real_size; ++i) {
			content[i] = CharT{};
		}
		real_size = count;
	}
	constexpr basic_string & append(view_type suffix) noexcept {
		if (suffix.size() > N - real_size) {
			detail::precondition_violated("ctc::basic_string: append beyond the capacity");
		}
		for (size_type i{0}; i < suffix.size(); ++i) {
			content[real_size + i] = suffix[i];
		}
		real_size += suffix.size();
		return *this;
	}
	constexpr basic_string & append(size_type count, CharT unit) noexcept {
		if (count > N - real_size) {
			detail::precondition_violated("ctc::basic_string: append beyond the capacity");
		}
		for (size_type i{0}; i < count; ++i) {
			content[real_size + i] = unit;
		}
		real_size += count;
		return *this;
	}
	constexpr basic_string & operator+=(view_type suffix) noexcept {
		return append(suffix);
	}
	constexpr basic_string & operator+=(CharT unit) noexcept {
		push_back(unit);
		return *this;
	}

	// search (all delegate to the constexpr std::basic_string_view)
	constexpr size_type find(view_type needle, size_type pos = 0) const noexcept {
		return view().find(needle, pos);
	}
	constexpr size_type find(CharT unit, size_type pos = 0) const noexcept {
		return view().find(unit, pos);
	}
	constexpr size_type rfind(view_type needle, size_type pos = npos) const noexcept {
		return view().rfind(needle, pos);
	}
	constexpr size_type rfind(CharT unit, size_type pos = npos) const noexcept {
		return view().rfind(unit, pos);
	}
	constexpr bool contains(view_type needle) const noexcept {
		return find(needle) != npos;
	}
	constexpr bool contains(CharT unit) const noexcept {
		return find(unit) != npos;
	}
	constexpr bool starts_with(view_type prefix) const noexcept {
		return view().starts_with(prefix);
	}
	constexpr bool starts_with(CharT unit) const noexcept {
		return view().starts_with(unit);
	}
	constexpr bool ends_with(view_type suffix) const noexcept {
		return view().ends_with(suffix);
	}
	constexpr bool ends_with(CharT unit) const noexcept {
		return view().ends_with(unit);
	}
	constexpr int compare(view_type rhs) const noexcept {
		return view().compare(rhs);
	}

	// substring, with the capacity computed at compile time
	template <size_t Pos, size_t Count = npos> constexpr auto substr() const noexcept {
		constexpr size_type available_capacity = (Pos < N) ? (N - Pos) : 0;
		constexpr size_type result_capacity = (Count < available_capacity) ? Count : available_capacity;
		basic_string<CharT, result_capacity> result;
		size_type out{0};
		for (size_type i{Pos}; i < real_size && out != result_capacity; ++i, ++out) {
			result.content[out] = content[i];
		}
		result.real_size = out;
		return result;
	}
	// substring as a view into this string (no copy)
	constexpr view_type substr(size_type pos, size_type count = npos) const noexcept {
		if (pos > real_size) {
			detail::precondition_violated("ctc::basic_string: substr position out of range");
		}
		const size_type available = real_size - pos;
		return view_type{content + pos, count < available ? count : available};
	}

	// a copy with a different capacity (checked to fit; shrunk<V>()
	// right-sizes to exactly size())
	template <size_t M> constexpr basic_string<CharT, M> with_capacity() const noexcept {
		if (real_size > M) {
			detail::precondition_violated("ctc::basic_string: content does not fit the new capacity");
		}
		basic_string<CharT, M> result;
		for (size_type i{0}; i < real_size; ++i) {
			result.content[i] = content[i];
		}
		result.real_size = real_size;
		return result;
	}
};

template <typename CharT, size_t M> basic_string(const CharT (&)[M]) -> basic_string<CharT, M - 1>;

CTC_EXPORT template <size_t N> using string = basic_string<char, N>;
CTC_EXPORT template <size_t N> using wstring = basic_string<wchar_t, N>;
CTC_EXPORT template <size_t N> using u8string = basic_string<char8_t, N>;
CTC_EXPORT template <size_t N> using u16string = basic_string<char16_t, N>;
CTC_EXPORT template <size_t N> using u32string = basic_string<char32_t, N>;

// make_string("literal"): deduction without spelling basic_string
// (alias-template CTAD needs clang 19, so the aliases cannot deduce)
CTC_EXPORT template <typename CharT, size_t M> constexpr auto make_string(const CharT (&literal)[M]) noexcept {
	return basic_string<CharT, M - 1>{literal};
}

// comparison: across capacities, and against anything convertible to a
// view (string literals, std::string_view, std::string). C++20
// synthesizes the reversed and secondary operators.
CTC_EXPORT template <typename CharT, size_t A, size_t B> constexpr bool operator==(const basic_string<CharT, A> & lhs, const basic_string<CharT, B> & rhs) noexcept {
	return lhs.view() == rhs.view();
}
CTC_EXPORT template <typename CharT, size_t A, size_t B> constexpr auto operator<=>(const basic_string<CharT, A> & lhs, const basic_string<CharT, B> & rhs) noexcept {
	return lhs.compare(rhs.view()) <=> 0;
}
CTC_EXPORT template <typename CharT, size_t A> constexpr bool operator==(const basic_string<CharT, A> & lhs, std::type_identity_t<std::basic_string_view<CharT>> rhs) noexcept {
	return lhs.view() == rhs;
}
CTC_EXPORT template <typename CharT, size_t A> constexpr auto operator<=>(const basic_string<CharT, A> & lhs, std::type_identity_t<std::basic_string_view<CharT>> rhs) noexcept {
	return lhs.compare(rhs) <=> 0;
}

// concatenation
CTC_EXPORT template <typename CharT, size_t A, size_t B> constexpr auto operator+(const basic_string<CharT, A> & lhs, const basic_string<CharT, B> & rhs) noexcept {
	basic_string<CharT, A + B> result;
	result.append(lhs.view());
	result.append(rhs.view());
	return result;
}
CTC_EXPORT template <typename CharT, size_t A, size_t M> constexpr auto operator+(const basic_string<CharT, A> & lhs, const CharT (&rhs)[M]) noexcept {
	return lhs + basic_string<CharT, M - 1>{rhs};
}
CTC_EXPORT template <typename CharT, size_t M, size_t B> constexpr auto operator+(const CharT (&lhs)[M], const basic_string<CharT, B> & rhs) noexcept {
	return basic_string<CharT, M - 1>{lhs} + rhs;
}

// iostream interoperability
CTC_EXPORT template <typename CharT, typename Traits, size_t A> std::basic_ostream<CharT, Traits> & operator<<(std::basic_ostream<CharT, Traits> & stream, const basic_string<CharT, A> & string) {
	return stream << std::basic_string_view<CharT, Traits>{string.data(), string.size()};
}

} // namespace ctc

#endif
