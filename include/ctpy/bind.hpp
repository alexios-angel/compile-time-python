#ifndef CTPY__BIND__HPP
#define CTPY__BIND__HPP

#include "version.hpp"
#include "object.hpp"
#include "parse.hpp"
#include "eval.hpp"
#include "builtins.hpp"
#include "exec.hpp"
#include "views.hpp"
#include "result.hpp"
#include "../ctc.hpp"
#include "../ctll/fixed_string.hpp"

#ifndef CTPY_IN_A_MODULE
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>
#endif

// The C++ <-> Python boundary.
//
// C++ -> Python (seed descriptors, passed to run<Src>(...)):
//   ctpy::arg<"name">(v)            seeds a global - scalars, string
//                                   literals / std::string_view, and
//                                   ctc::vector / ctc::map / ctc::string
//                                   (nested combinations included) lift in
//   ctpy::file<"path", "contents">  mounts a compile-time VFS file;
//                                   open("path") returns it, open() of an
//                                   unmounted path raises OSError. When
//                                   std::embed lands, ctpy::file<"path">
//                                   gains a real-file overload and nothing
//                                   else changes.
//   ctpy::stdin_text<"...">         what input() reads, line by line
//                                   (EOFError once it runs dry)
//   ctpy::pymodule<"name", src>     mounts a user module on the import
//                                   registry - v0.1 fixes the descriptor
//                                   SHAPE only (it validates the source
//                                   parses); import execution is deferred
//
// Python -> C++:
//   ctpy::lift<ctc::vector<int, 16>>(out["xs"])   a value view into a ctc
//   container (vector/map/string, nested), then ctc::shrunk as usual.
//
// And the parsed-module handle: ctpy::module<src> parses once,
// .call<"fn">(args...) executes the module body and invokes the def,
// returning the same result shape as run<> with the return value on
// the result() channel.

namespace ctpy {

namespace detail {

// a ctll::fixed_string NTTP narrowed to char, in static storage - the
// views seed descriptors hand out must outlive every State and result
template <ctll::fixed_string S> consteval auto narrow() noexcept {
	ctc::string<S.size()> out{};
	for (std::size_t at = 0; at < S.size(); ++at) {
		out.push_back(static_cast<char>(static_cast<unsigned char>(S[at])));
	}
	return out;
}
template <ctll::fixed_string S> inline constexpr auto narrowed = narrow<S>();
template <ctll::fixed_string S> inline constexpr std::string_view narrow_view = narrowed<S>.view();

// --- lifting C++ values IN: one interpreter object per payload ---------------

template <typename St, typename E, std::size_t N>
constexpr std::uint32_t make_object(St & st, const ctc::vector<E, N> & payload);
template <typename St, typename CharT, std::size_t N>
constexpr std::uint32_t make_object(St & st, const ctc::basic_string<CharT, N> & payload);
template <typename St, typename K, typename V, std::size_t N, typename Compare>
constexpr std::uint32_t make_object(St & st, const ctc::map<K, V, N, Compare> & payload);

// scalars and string-ish payloads (the generic case)
template <typename St, typename T>
constexpr std::uint32_t make_object(St & st, const T & payload) {
	if constexpr (std::is_same_v<T, bool>) {
		return st.make_bool(payload);
	} else if constexpr (std::is_integral_v<T>) {
		return st.make_int(static_cast<long long>(payload));
	} else if constexpr (std::is_floating_point_v<T>) {
		return st.make_float(static_cast<double>(payload));
	} else if constexpr (std::is_convertible_v<const T &, std::string_view>) {
		return st.make_str(std::string_view{payload});
	} else {
		static_assert(sizeof(T) == 0,
			"ctpy::arg: unsupported payload (scalars, string literals, std::string_view, "
			"ctc::vector / ctc::map / ctc::string lift in)");
		return st.none();
	}
}

// ctc::vector -> list (elements lift recursively, display-style: all
// elements first, then one contiguous run)
template <typename St, typename E, std::size_t N>
constexpr std::uint32_t make_object(St & st, const ctc::vector<E, N> & payload) {
	std::uint32_t items[N ? N : 1]{};
	for (std::size_t at = 0; at < payload.size(); ++at) {
		items[at] = make_object(st, payload[at]);
	}
	const std::uint32_t first = static_cast<std::uint32_t>(st.a.objs.size());
	for (std::size_t at = 0; at < payload.size(); ++at) {
		st.a.objs.push_back(st.a.objs[items[at]]);
	}
	return st.push(Object{.kind = Kind::list,
	                      .first = first,
	                      .count = static_cast<std::uint32_t>(payload.size())});
}

// ctc::string -> str
template <typename St, typename CharT, std::size_t N>
constexpr std::uint32_t make_object(St & st, const ctc::basic_string<CharT, N> & payload) {
	static_assert(std::is_same_v<CharT, char>, "ctpy::arg: only char strings lift in");
	return st.make_str(payload.view());
}

// ctc::map -> dict (already deduplicated and ordered by its Compare;
// that order becomes the dict's insertion order)
template <typename St, typename K, typename V, std::size_t N, typename Compare>
constexpr std::uint32_t make_object(St & st, const ctc::map<K, V, N, Compare> & payload) {
	std::uint32_t keys[N ? N : 1]{};
	std::uint32_t values[N ? N : 1]{};
	for (std::size_t at = 0; at < payload.size(); ++at) {
		keys[at] = make_object(st, payload.data()[at].first);
		values[at] = make_object(st, payload.data()[at].second);
	}
	const std::uint32_t first = static_cast<std::uint32_t>(st.a.pairs.size());
	for (std::size_t at = 0; at < payload.size(); ++at) {
		st.a.pairs.push_back(Pair{keys[at], values[at]});
	}
	return st.push(Object{.kind = Kind::dict,
	                      .first = first,
	                      .count = static_cast<std::uint32_t>(payload.size())});
}

template <typename T> inline constexpr bool is_ctc_payload = false;
template <typename E, std::size_t N> inline constexpr bool is_ctc_payload<ctc::vector<E, N>> = true;
template <typename CharT, std::size_t N>
inline constexpr bool is_ctc_payload<ctc::basic_string<CharT, N>> = true;
template <typename K, typename V, std::size_t N, typename Compare>
inline constexpr bool is_ctc_payload<ctc::map<K, V, N, Compare>> = true;

} // namespace detail

// --- arg<"name">(value): seed a global ---------------------------------------

CTPY_EXPORT template <ctll::fixed_string Name, typename T> struct arg_t {
	T payload{};

	static constexpr std::string_view name() noexcept {
		return detail::narrow_view<Name>;
	}
	template <typename St> constexpr void seed(St & st) const {
		st.bind(name(), detail::make_object(st, payload));
	}
};

// named-argument injection:  run<"y = x * 2\n">(ctpy::arg<"x">(21))
CTPY_EXPORT template <ctll::fixed_string Name, typename T>
constexpr auto arg(const T & payload) noexcept {
	if constexpr (std::is_arithmetic_v<T> || detail::is_ctc_payload<T>) {
		return arg_t<Name, T>{payload};
	} else if constexpr (std::is_convertible_v<const T &, std::string_view>) {
		// string literals (and other char sources with static storage):
		// the descriptor keeps a view, the seed copies it into the pool
		return arg_t<Name, std::string_view>{std::string_view{payload}};
	} else {
		static_assert(sizeof(T) == 0,
			"ctpy::arg: unsupported payload (scalars, string literals, std::string_view, "
			"ctc::vector / ctc::map / ctc::string lift in)");
		return arg_t<Name, int>{};
	}
}

// --- file<"path", "contents">: the compile-time VFS ---------------------------

CTPY_EXPORT template <ctll::fixed_string Path, ctll::fixed_string Contents> struct file_t {
	static constexpr std::string_view path() noexcept {
		return detail::narrow_view<Path>;
	}
	static constexpr std::string_view contents() noexcept {
		return detail::narrow_view<Contents>;
	}
	template <typename St> constexpr void seed(St & st) const {
		st.vfs.push_back(vfs_entry{path(), contents()});
	}
};

// mount for open():  run<...>(ctpy::file<"config.txt", "timeout=250\n">)
// (std::embed-ready: a real-file overload slots in as file<"path">
// once C++ can embed at compile time - nothing else changes)
CTPY_EXPORT template <ctll::fixed_string Path, ctll::fixed_string Contents>
inline constexpr file_t<Path, Contents> file{};

// --- stdin_text<"...">: what input() reads -------------------------------------

CTPY_EXPORT template <ctll::fixed_string Text> struct stdin_t {
	static constexpr std::string_view text() noexcept {
		return detail::narrow_view<Text>;
	}
	template <typename St> constexpr void seed(St & st) const {
		st.stdin_content = text();
		st.stdin_at = 0;
	}
};

CTPY_EXPORT template <ctll::fixed_string Text>
inline constexpr stdin_t<Text> stdin_text{};

// --- pymodule<"name", src>: the import registry seam ---------------------------

// v0.1 fixes the descriptor SHAPE only: mounting validates the module
// source parses (a parse-demanding entry point, family policy), but
// `import` execution is deferred, so the seed records nothing yet.
CTPY_EXPORT template <ctll::fixed_string Name, ctll::fixed_string Src> struct pymodule_t {
	static_assert(detail::require_valid<Src>()); // hard-errors NAMING the stage

	static constexpr std::string_view name() noexcept {
		return detail::narrow_view<Name>;
	}
	static constexpr std::string_view source() noexcept {
		return detail::narrow_view<Src>;
	}
	template <typename St> constexpr void seed(St &) const noexcept { }
};

CTPY_EXPORT template <ctll::fixed_string Name, ctll::fixed_string Src>
inline constexpr pymodule_t<Name, Src> pymodule{};

// --- lift<Container>(value): Python values OUT into ctc containers -------------

namespace detail {

template <typename T> struct lifter {
	static_assert(sizeof(T) == 0,
		"ctpy::lift<T>: T must be ctc::vector / ctc::map / ctc::string "
		"(elements may be arithmetic, std::string_view, or nested ctc containers)");
};

template <typename T> constexpr T lift_one(const value & v) noexcept {
	if constexpr (std::is_arithmetic_v<T> || std::is_same_v<T, std::string_view>) {
		return v.template to<T>();
	} else {
		return lifter<T>::from(v);
	}
}

// vector from anything iterable (a dict yields its keys, like list(d))
template <typename E, std::size_t N> struct lifter<ctc::vector<E, N>> {
	static constexpr ctc::vector<E, N> from(const value & v) noexcept {
		ctc::vector<E, N> out{};
		const long long total = static_cast<long long>(v.size());
		for (long long at = 0; at < total; ++at) {
			out.push_back(lift_one<E>(v.element(at)));
		}
		return out;
	}
};

// string from a str (or an open file's contents)
template <typename CharT, std::size_t N> struct lifter<ctc::basic_string<CharT, N>> {
	static_assert(std::is_same_v<CharT, char>, "ctpy::lift: only char strings lift out");
	static constexpr ctc::basic_string<CharT, N> from(const value & v) noexcept {
		ctc::basic_string<CharT, N> out{};
		out.append(v.str());
		return out;
	}
};

// map from a dict (keys re-sort under the map's Compare)
template <typename K, typename V, std::size_t N, typename Compare>
struct lifter<ctc::map<K, V, N, Compare>> {
	static constexpr ctc::map<K, V, N, Compare> from(const value & v) noexcept {
		ctc::map<K, V, N, Compare> out{};
		if (v.kind != Kind::dict) {
			return out;
		}
		for (std::uint32_t at = 0; at < v.count; ++at) {
			const Pair entry = v.pairs[v.first + at];
			out.insert_or_assign(
				lift_one<K>(make_value(v.objs, v.pairs, v.chars, v.objs[entry.key])),
				lift_one<V>(make_value(v.objs, v.pairs, v.chars, v.objs[entry.value])));
		}
		return out;
	}
};

} // namespace detail

// lift a result value into a ctc container (right-size afterwards with
// ctc::shrunk, the usual two-step). Misses and kind mismatches lift as
// EMPTY - the null-object policy of the views carries through; an
// overflowing capacity is the ctc precondition trap (raise N).
CTPY_EXPORT template <typename Container> constexpr Container lift(const value & v) noexcept {
	return detail::lifter<Container>::from(v);
}

// --- module<src>: parse once, call defs -----------------------------------------

namespace detail {

template <ctll::fixed_string Src, ctll::fixed_string Fn, typename ArenaT, typename... A>
constexpr auto call_to_flat(const A &... args) {
	static_assert(require_valid<Src>()); // hard-errors NAMING the stage
	State<ArenaT> st{};
	st.line_map = prelex_raw<Src>.lines.data();
	st.line_map_count = static_cast<std::uint32_t>(prelex_raw<Src>.lines.size());
	(void)exec_node<parsed_module<Src>, State<ArenaT>>(st);
	st.current_line = 0; // the .call<> dispatch has no Python call site
	std::uint32_t result = not_found;
	if (!st.raised) {
		const std::uint32_t bound = st.lookup(narrow_view<Fn>);
		if (bound == not_found) {
			st.raise_error(ex_kind::NameError,
			               {"name '", narrow_view<Fn>, "' is not defined"});
		} else if (st.a.objs[bound].kind != Kind::function) {
			st.raise_error(ex_kind::TypeError,
			               {"'", type_name(st.a.objs[bound].kind), "' object is not callable"});
		} else {
			const Object fn = st.a.objs[bound]; // copy: lifting args grows the pool
			std::uint32_t argv[sizeof...(A) + 1]{};
			std::size_t at = 0;
			((argv[at++] = make_object(st, args)), ...);
			(void)at;
			result = st.thunks[static_cast<std::size_t>(fn.i)](st, fn, argv, sizeof...(A));
		}
	}
	return flatten<flat_caps<ArenaT>>(st, st.raised ? not_found : result);
}

} // namespace detail

// a parsed module as a value: executes its body, then invokes a def.
//
//   constexpr auto lib = ctpy::module<"def double(x):\n    return 2 * x\n">;
//   static_assert(lib.call<"double">(21).to<int>() == 42);
//
// Arguments lift exactly like arg<> payloads; the return value rides
// the result() channel (to<T>()/str() forward to it), stdout and any
// exception come along like a run<>.
CTPY_EXPORT template <ctll::fixed_string Src, typename ArenaT = Arena<>> struct module_t {
	static_assert(detail::require_valid<Src>()); // hard-errors NAMING the stage

	template <ctll::fixed_string Fn, typename... A>
	constexpr auto call(const A &... args) const noexcept {
		return detail::call_to_flat<Src, Fn, ArenaT>(args...);
	}
};

CTPY_EXPORT template <ctll::fixed_string Src, typename ArenaT = Arena<>>
inline constexpr module_t<Src, ArenaT> module{};

} // namespace ctpy

#endif
