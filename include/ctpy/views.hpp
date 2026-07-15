#ifndef CTPY__VIEWS__HPP
#define CTPY__VIEWS__HPP

#include "version.hpp"
#include "object.hpp"

#ifndef CTPY_IN_A_MODULE
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#endif

// The uniform view over a Python result value: ctpy::value. The
// value-level analog of ctjson's views.hpp - but where ctjson's
// documents are TYPES (one static array per container type), a ctpy
// result is a VALUE, so a view carries pointers into the flattened
// pools that result.hpp copied out of the dead interpreter arena
// (objs / pairs / chars) plus this value's run inside them. When the
// result lives in a static constexpr variable - the family test idiom,
// `constexpr auto out = ctpy::run<...>()` - every pointer aims at
// static storage and the views persist to runtime for free.
//
//   static_assert(out["answer"].to<int>() == 55);
//   static_assert(out["d"]["a"]["b"][1].to<int>() == 20);  // chaining
//   for (ctpy::value v : out["xs"]) { ... }                // begin/end
//   for (ctpy::global_view g : out.globals()) { ... }      // iteration
//
// Misses follow the null-object pattern of any dynamic API: indexing a
// name that was never bound, a key a dict does not hold, or an element
// past the end yields a value with exists() == false and Kind::none
// that indexes on harmlessly - out["a"]["b"][0] never hard-errors.
// Subscripts keep Python semantics: a dict looks its key up (str keys
// via operator[](string_view), int keys via operator[](long long)), a
// list/tuple/str/range indexes positionally with negative indices
// counting from the end, and a set is not subscriptable.

namespace ctpy {

CTPY_EXPORT struct value_iterator;

CTPY_EXPORT struct value {
	// which fields carry the payload depends on kind:
	//   boolean/int_    int_value
	//   float_          float_value
	//   str/file        [text_data, text_data+text_count) - the content
	//   range           objs[first, first+3) are the start/stop/step ints
	//   tuple/list/set  objs[first, first+count) - the element run
	//   dict            pairs[first, first+count) - key/value obj indices
	Kind kind = Kind::none;
	bool present = false; // false = the null-object view (a miss)
	long long int_value = 0;
	double float_value = 0.0;
	const char * text_data = nullptr;
	std::uint32_t text_count = 0;
	// the flattened pools the runs above index into (container kinds)
	const Object * objs = nullptr;
	const Pair * pairs = nullptr;
	const char * chars = nullptr;
	std::uint32_t first = 0;
	std::uint32_t count = 0;

	// was there a value at all? (a bound None exists; a miss does not)
	constexpr bool exists() const noexcept {
		return present;
	}

	// Python len(v) for the sized kinds (0 for scalars and misses)
	constexpr std::size_t size() const noexcept {
		switch (kind) {
			case Kind::str:
			case Kind::file:
				return text_count;
			case Kind::tuple:
			case Kind::list:
			case Kind::set:
			case Kind::dict:
				return count;
			case Kind::range:
				return static_cast<std::size_t>(detail::range_len(
					objs[first].i, objs[first + 1].i, objs[first + 2].i));
			default:
				return 0;
		}
	}
	constexpr bool empty() const noexcept {
		return size() == 0;
	}

	// the content of a str (or an open file); empty for everything else
	constexpr std::string_view str() const noexcept {
		if (kind != Kind::str && kind != Kind::file) {
			return std::string_view{};
		}
		return std::string_view{text_data, text_count};
	}

	// scalar conversion; T = bool applies Python truthiness
	template <typename T> constexpr T to() const noexcept {
		if constexpr (std::is_same_v<T, std::string_view>) {
			return str();
		} else if constexpr (std::is_same_v<T, bool>) {
			switch (kind) {
				case Kind::boolean:
				case Kind::int_: return int_value != 0;
				case Kind::float_: return float_value != 0.0;
				case Kind::none: return false;
				default: return size() != 0 || kind == Kind::function || kind == Kind::file;
			}
		} else if constexpr (std::is_floating_point_v<T>) {
			return kind == Kind::float_ ? static_cast<T>(float_value) : static_cast<T>(int_value);
		} else {
			static_assert(std::is_integral_v<T>,
				"ctpy::value::to<T>: T must be arithmetic or std::string_view");
			return kind == Kind::float_ ? static_cast<T>(float_value) : static_cast<T>(int_value);
		}
	}

	// dict lookup by str key (misses and non-dicts yield the null view)
	constexpr value operator[](std::string_view key) const noexcept;
	// Python subscript: positional for list/tuple/str/range (negative
	// counts from the end), key lookup for a dict with int keys
	constexpr value operator[](long long at) const noexcept;

	// element `at` of an in-range iteration: list/tuple/set elements,
	// str characters, range ints, dict KEYS (Python iteration order)
	constexpr value element(long long at) const noexcept;

	constexpr value_iterator begin() const noexcept;
	constexpr value_iterator end() const noexcept;
};

// materialize the view of one flattened object
CTPY_EXPORT constexpr value make_value(const Object * objs, const Pair * pairs,
                                       const char * chars, const Object & object) noexcept {
	value out{};
	out.present = true;
	out.kind = object.kind;
	switch (object.kind) {
		case Kind::none:
			break;
		case Kind::boolean:
		case Kind::int_:
			out.int_value = object.i;
			break;
		case Kind::float_:
			out.float_value = object.f;
			break;
		case Kind::str:
		case Kind::file:
			out.text_data = chars + object.first;
			out.text_count = object.count;
			break;
		case Kind::range:
		case Kind::tuple:
		case Kind::list:
		case Kind::set:
		case Kind::dict:
			out.objs = objs;
			out.pairs = pairs;
			out.chars = chars;
			out.first = object.first;
			out.count = object.count;
			break;
		case Kind::function:
			break; // opaque: only the kind is observable
	}
	return out;
}

constexpr value value::operator[](std::string_view key) const noexcept {
	if (kind != Kind::dict) {
		return value{};
	}
	for (std::uint32_t at = 0; at < count; ++at) {
		const Object & held = objs[pairs[first + at].key];
		if (held.kind == Kind::str &&
		    std::string_view{chars + held.first, held.count} == key) {
			return make_value(objs, pairs, chars, objs[pairs[first + at].value]);
		}
	}
	return value{};
}

constexpr value value::operator[](long long at) const noexcept {
	if (kind == Kind::dict) { // Python semantics: d[0] is a KEY lookup
		for (std::uint32_t entry = 0; entry < count; ++entry) {
			const Object & held = objs[pairs[first + entry].key];
			if ((held.kind == Kind::int_ || held.kind == Kind::boolean) && held.i == at) {
				return make_value(objs, pairs, chars, objs[pairs[first + entry].value]);
			}
		}
		return value{};
	}
	const long long length = static_cast<long long>(size());
	if (at < 0) {
		at += length; // negative indices count from the end, like Python
	}
	if (at < 0 || at >= length || kind == Kind::set) {
		return value{}; // out of range (or not subscriptable): the null view
	}
	return element(at);
}

constexpr value value::element(long long at) const noexcept {
	switch (kind) {
		case Kind::str:
		case Kind::file: {
			value out{};
			out.present = true;
			out.kind = Kind::str;
			out.text_data = text_data + at;
			out.text_count = 1;
			return out;
		}
		case Kind::range: {
			value out{};
			out.present = true;
			out.kind = Kind::int_;
			out.int_value = objs[first].i + at * objs[first + 2].i;
			return out;
		}
		case Kind::dict:
			return make_value(objs, pairs, chars,
			                  objs[pairs[first + static_cast<std::uint32_t>(at)].key]);
		case Kind::tuple:
		case Kind::list:
		case Kind::set:
			return make_value(objs, pairs, chars, objs[first + static_cast<std::uint32_t>(at)]);
		default:
			return value{};
	}
}

// iteration: begin/end materialize each element view on dereference
// (the elements of one container have no common storage to point at)
CTPY_EXPORT struct value_iterator {
	value container{};
	long long at = 0;

	constexpr value operator*() const noexcept {
		return container.element(at);
	}
	constexpr value_iterator & operator++() noexcept {
		++at;
		return *this;
	}
	constexpr value_iterator operator++(int) noexcept {
		const value_iterator before = *this;
		++at;
		return before;
	}
	friend constexpr bool operator==(const value_iterator & lhs, const value_iterator & rhs) noexcept {
		return lhs.at == rhs.at;
	}
};

constexpr value_iterator value::begin() const noexcept {
	return value_iterator{*this, 0};
}
constexpr value_iterator value::end() const noexcept {
	return value_iterator{*this, static_cast<long long>(size())};
}

// --- the globals of a result, iterable as name/value pairs -------------------

// one flattened global binding. The name pointer aims into the static
// storage of an ast text<> node (or an arg<> descriptor's name), both
// of which outlive every result; keeping raw pointer + size instead of
// a std::string_view makes the type STRUCTURAL, so a right-sized
// bindings pool can ride through ctc::shrunk as an NTTP.
CTPY_EXPORT struct flat_global {
	const char * name_data = nullptr;
	std::uint32_t name_count = 0;
	std::uint32_t obj = 0; // index of the value's header in the flat objs pool

	constexpr std::string_view name() const noexcept {
		return std::string_view{name_data, name_count};
	}
};

CTPY_EXPORT struct global_view {
	std::string_view name{};
	value val{};
};

CTPY_EXPORT struct globals_iterator {
	const flat_global * at = nullptr;
	const Object * objs = nullptr;
	const Pair * pairs = nullptr;
	const char * chars = nullptr;

	constexpr global_view operator*() const noexcept {
		return global_view{at->name(), make_value(objs, pairs, chars, objs[at->obj])};
	}
	constexpr globals_iterator & operator++() noexcept {
		++at;
		return *this;
	}
	constexpr globals_iterator operator++(int) noexcept {
		const globals_iterator before = *this;
		++at;
		return before;
	}
	friend constexpr bool operator==(const globals_iterator & lhs, const globals_iterator & rhs) noexcept {
		return lhs.at == rhs.at;
	}
};

CTPY_EXPORT struct globals_range {
	globals_iterator from{};
	const flat_global * upto = nullptr;

	constexpr globals_iterator begin() const noexcept {
		return from;
	}
	constexpr globals_iterator end() const noexcept {
		return globals_iterator{upto, from.objs, from.pairs, from.chars};
	}
	constexpr std::size_t size() const noexcept {
		return static_cast<std::size_t>(upto - from.at);
	}
	constexpr bool empty() const noexcept {
		return upto == from.at;
	}
};

} // namespace ctpy

#endif
