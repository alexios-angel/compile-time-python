#ifndef CTPY__TEXT__HPP
#define CTPY__TEXT__HPP

#include "version.hpp"

#ifndef CTPY_IN_A_MODULE
#include <cstddef>
#include <string_view>
#endif

// A compile-time string as a type: one non-type template parameter per
// character (the ctlark text<> pattern). The semantic actions
// accumulate identifiers, numbers and string bodies into these while
// CTLL parses; two texts with the same characters are the same TYPE,
// which is what makes AST-shape asserts plain std::is_same_v checks.

namespace ctpy {

CTPY_EXPORT template <auto... Chars> struct text {
	// null-terminated so c_str()/data() work as C strings; size() excludes it
	static constexpr char storage[sizeof...(Chars) + 1]{static_cast<char>(Chars)..., '\0'};

	static constexpr const char * c_str() noexcept {
		return storage;
	}
	static constexpr size_t size() noexcept {
		return sizeof...(Chars);
	}
	static constexpr bool empty() noexcept {
		return sizeof...(Chars) == 0;
	}
	static constexpr std::string_view view() noexcept {
		return std::string_view{storage, sizeof...(Chars)};
	}
	constexpr operator std::string_view() const noexcept {
		return view();
	}
	template <auto... Rhs> constexpr bool operator==(text<Rhs...>) const noexcept {
		return view() == text<Rhs...>::view();
	}
	friend constexpr bool operator==(text, std::string_view rhs) noexcept {
		return view() == rhs;
	}
	friend constexpr bool operator==(std::string_view lhs, text) noexcept {
		return lhs == view();
	}
};

// the string_view of a text TYPE (handy in static_asserts where only
// the type is at hand, e.g. to_sv<decltype(t)>() or to_sv<T>())
CTPY_EXPORT template <typename Text> constexpr std::string_view to_sv() noexcept {
	return Text::view();
}

// ... and of a text VALUE
CTPY_EXPORT template <auto... Chars> constexpr std::string_view to_sv(text<Chars...>) noexcept {
	return text<Chars...>::view();
}

} // namespace ctpy

#endif
