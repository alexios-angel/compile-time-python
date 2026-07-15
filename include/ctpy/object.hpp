#ifndef CTPY__OBJECT__HPP
#define CTPY__OBJECT__HPP

#include "version.hpp"
#include "../ctc.hpp"

#ifndef CTPY_IN_A_MODULE
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string_view>
#endif

// The interpreter's object model. Everything here is TRANSIENT: a
// State lives inside one constexpr evaluation, is mutated freely by
// eval<>/exec<>, and dies when the evaluation returns its right-sized
// result (M8). Nothing in this header is ever an NTTP, so plain tagged
// structs and real mutation are fine - no structural-type tricks.
//
// Objects live in append-only pools (ctc vectors) inside an Arena and
// reference each other exclusively by pool INDEX, never by pointer:
// a str is an index-range into the char pool, a list/tuple/set is an
// index-range into the object pool, a dict is an index-range into the
// pair pool. There is no GC - the arena only grows, and one final
// right-sizing pass (M8) copies the reachable result out. Pool
// capacities are template parameters with defaults; a run that
// overflows a pool fails the build with the ctc precondition message
// (raise the capacity for that run).

namespace ctpy {

// --- the value tag ------------------------------------------------------

CTPY_EXPORT enum class Kind : unsigned char {
	none,
	boolean,
	int_,
	float_,
	str,
	range,
	tuple,
	list,
	set,
	dict,
	function,
};

// Python's name for a Kind, as spelled by type(x).__name__ (used in
// TypeError messages: "unsupported operand type(s) for +: 'int' and 'str'")
CTPY_EXPORT constexpr std::string_view type_name(Kind kind) noexcept {
	switch (kind) {
		case Kind::none: return "NoneType";
		case Kind::boolean: return "bool";
		case Kind::int_: return "int";
		case Kind::float_: return "float";
		case Kind::str: return "str";
		case Kind::range: return "range";
		case Kind::tuple: return "tuple";
		case Kind::list: return "list";
		case Kind::set: return "set";
		case Kind::dict: return "dict";
		case Kind::function: return "function";
	}
	return "object";
}

// --- objects and the pools they index -----------------------------------

// one Python value; which fields are meaningful depends on kind:
//   boolean/int_  i                       (True is i==1)
//   float_        f
//   str           [first, first+count) of Arena::chars
//   range         [first, first+3) of Arena::objs are the start/stop/step ints
//   tuple/list/set[first, first+count) of Arena::objs
//   dict          [first, first+count) of Arena::pairs
//   function      i = State::thunks index; [first, first+count) of
//                 Arena::objs are the def-time-evaluated defaults
CTPY_EXPORT struct Object {
	Kind kind = Kind::none;
	long long i = 0;
	double f = 0.0;
	std::uint32_t first = 0;
	std::uint32_t count = 0;
};

// one dict entry: object-pool indices of the key and the value
CTPY_EXPORT struct Pair {
	std::uint32_t key = 0;
	std::uint32_t value = 0;
};

// one name in a scope. The name view points into the static storage of
// an ast text<> node (or a caller-provided literal) - both outlive any
// State, so a view is safe here.
CTPY_EXPORT struct Binding {
	std::string_view name{};
	std::uint32_t obj = 0;
};

// one call frame: its locals are the [first, first+count) range of the
// binding pool. Frames nest LIFO, so the pool truncates back to
// `first` on return. Module level is NOT a frame - globals occupy
// [0, State::globals_count) of the pool and only grow while no frame
// is live (there are no closures in the v0.1 subset).
CTPY_EXPORT struct Frame {
	std::uint32_t first = 0;
	std::uint32_t count = 0;
};

// the interpreter heap: append-only pools, capacities as template
// parameters (defaults sized for test-suite scripts - override per run
// via State<Arena<...>> when a script outgrows them)
CTPY_EXPORT template <
	std::size_t Objs = 2048,
	std::size_t Chars = 8192,
	std::size_t Pairs = 512,
	std::size_t Frames = 512,
	std::size_t Out = 8192>
struct Arena {
	static constexpr std::size_t objs_capacity = Objs;
	static constexpr std::size_t chars_capacity = Chars;
	static constexpr std::size_t pairs_capacity = Pairs;
	static constexpr std::size_t frames_capacity = Frames;
	static constexpr std::size_t out_capacity = Out;

	ctc::vector<Object, Objs> objs{};
	ctc::vector<char, Chars> chars{};
	ctc::vector<Pair, Pairs> pairs{};
	ctc::vector<Binding, Frames> frames{}; // all bindings: globals, then LIFO frame locals
	ctc::vector<char, Out> out{};          // captured stdout (print writes here)
};

namespace detail {

// len(range(start, stop, step)) - step is never 0 (range() raises first)
constexpr long long range_len(long long start, long long stop, long long step) noexcept {
	if (step > 0) {
		return start < stop ? (stop - start + step - 1) / step : 0;
	}
	return start > stop ? (start - stop - step - 1) / (-step) : 0;
}

// spell a small count for an error message ("expected 2, got 3")
constexpr ctc::string<20> dec(long long value) noexcept {
	ctc::string<20> out{};
	unsigned long long magnitude = 0;
	if (value < 0) {
		out.push_back('-');
		magnitude = 0ULL - static_cast<unsigned long long>(value);
	} else {
		magnitude = static_cast<unsigned long long>(value);
	}
	char digits[20]{};
	std::size_t used = 0;
	do {
		digits[used++] = static_cast<char>('0' + static_cast<char>(magnitude % 10ULL));
		magnitude /= 10ULL;
	} while (magnitude != 0);
	while (used > 0) {
		out.push_back(digits[--used]);
	}
	return out;
}

} // namespace detail

// --- Python exceptions (soft channel - NEVER C++ exceptions) -------------

// A raising script is not a build failure: eval/exec set State::raised
// and stash one of these, every step short-circuits out, and the
// result reports ok()==false with the exception queryable. Enumerators
// use Python's own spelling so `out.exception() == ctpy::TypeError`
// reads like the traceback would.
CTPY_EXPORT enum class ex_kind : unsigned char {
	none,
	ZeroDivisionError,
	TypeError,
	NameError,
	ValueError,
	OverflowError,
	IndexError,
	KeyError,
	AttributeError,
	RecursionError,
	StopIteration,
	OSError,
	TabError,
	SyntaxError,
};

CTPY_EXPORT constexpr std::string_view ex_name(ex_kind kind) noexcept {
	switch (kind) {
		case ex_kind::none: return "";
		case ex_kind::ZeroDivisionError: return "ZeroDivisionError";
		case ex_kind::TypeError: return "TypeError";
		case ex_kind::NameError: return "NameError";
		case ex_kind::ValueError: return "ValueError";
		case ex_kind::OverflowError: return "OverflowError";
		case ex_kind::IndexError: return "IndexError";
		case ex_kind::KeyError: return "KeyError";
		case ex_kind::AttributeError: return "AttributeError";
		case ex_kind::RecursionError: return "RecursionError";
		case ex_kind::StopIteration: return "StopIteration";
		case ex_kind::OSError: return "OSError";
		case ex_kind::TabError: return "TabError";
		case ex_kind::SyntaxError: return "SyntaxError";
	}
	return "";
}

// the enumerators as ctpy:: constants, so results compare the way the
// plan spells it: out.exception() == ctpy::ZeroDivisionError
CTPY_EXPORT inline constexpr ex_kind ZeroDivisionError = ex_kind::ZeroDivisionError;
CTPY_EXPORT inline constexpr ex_kind TypeError = ex_kind::TypeError;
CTPY_EXPORT inline constexpr ex_kind NameError = ex_kind::NameError;
CTPY_EXPORT inline constexpr ex_kind ValueError = ex_kind::ValueError;
CTPY_EXPORT inline constexpr ex_kind OverflowError = ex_kind::OverflowError;
CTPY_EXPORT inline constexpr ex_kind IndexError = ex_kind::IndexError;
CTPY_EXPORT inline constexpr ex_kind KeyError = ex_kind::KeyError;
CTPY_EXPORT inline constexpr ex_kind AttributeError = ex_kind::AttributeError;
CTPY_EXPORT inline constexpr ex_kind RecursionError = ex_kind::RecursionError;
CTPY_EXPORT inline constexpr ex_kind StopIteration = ex_kind::StopIteration;
CTPY_EXPORT inline constexpr ex_kind OSError = ex_kind::OSError;
CTPY_EXPORT inline constexpr ex_kind TabError = ex_kind::TabError;
CTPY_EXPORT inline constexpr ex_kind SyntaxError = ex_kind::SyntaxError;

// the raised exception: type, message, source line (0 until the M9
// diagnostics pass threads line numbers through). The message is a
// fixed-capacity copy that TRUNCATES rather than overflow - an error
// message must never itself fail the build.
CTPY_EXPORT struct PyError {
	static constexpr std::size_t message_capacity = 120;

	ex_kind type = ex_kind::none;
	int line = 0;
	ctc::string<message_capacity> text{};

	constexpr std::string_view name() const noexcept {
		return ex_name(type);
	}
	constexpr std::string_view message() const noexcept {
		return text.view();
	}
	constexpr PyError & append(std::string_view part) noexcept {
		const std::size_t room = message_capacity - text.size();
		text.append(part.size() <= room ? part : part.substr(0, room));
		return *this;
	}
	// a PyError IS its exception type for comparison purposes
	friend constexpr bool operator==(const PyError & error, ex_kind kind) noexcept {
		return error.type == kind;
	}
};

// --- the interpreter state ------------------------------------------------

// index sentinel: name not bound
CTPY_EXPORT inline constexpr std::uint32_t not_found = 0xFFFFFFFFu;

CTPY_EXPORT template <typename ArenaT = Arena<>> struct State {
	using arena_type = ArenaT;

	// Calling a def'd function goes through a THUNK: exec.hpp
	// instantiates one constexpr function per (def AST type, State
	// type) and registers its pointer here when the def executes. The
	// pointer ERASES the def's type, so the value-level Object can
	// reference the type-level AST: a function Object stores its thunk
	// index in `i` and its defaults as an object-pool run.
	using function_thunk = std::uint32_t (*)(State &, const Object &, const std::uint32_t *, std::size_t);

	// singleton object-pool indices, seeded by the constructor
	static constexpr std::uint32_t none_index = 0;
	static constexpr std::uint32_t false_index = 1;
	static constexpr std::uint32_t true_index = 2;

	ArenaT a{};
	ctc::vector<Frame, 128> stack{};        // live call frames (deepest last)
	ctc::vector<function_thunk, 256> thunks{}; // one slot per distinct executed def
	std::uint32_t globals_count = 0;        // [0, globals_count) of a.frames are globals
	std::uint32_t retval = none_index;      // the value carried by Flow::return_
	bool raised = false;
	PyError error{};
	int depth = 0;                          // live Python calls (the soft recursion guard)
	int recursion_limit = 100;              // RecursionError fires here, far below -fconstexpr-depth

	constexpr State() {
		a.objs.push_back(Object{});                                    // None
		a.objs.push_back(Object{.kind = Kind::boolean, .i = 0});       // False
		a.objs.push_back(Object{.kind = Kind::boolean, .i = 1});       // True
	}

	// --- allocation -----------------------------------------------------

	constexpr std::uint32_t none() const noexcept {
		return none_index;
	}
	constexpr std::uint32_t make_bool(bool value) noexcept {
		return value ? true_index : false_index;
	}
	constexpr std::uint32_t push(const Object & object) {
		const std::uint32_t index = static_cast<std::uint32_t>(a.objs.size());
		a.objs.push_back(object);
		return index;
	}
	constexpr std::uint32_t make_int(long long value) {
		return push(Object{.kind = Kind::int_, .i = value});
	}
	constexpr std::uint32_t make_float(double value) {
		return push(Object{.kind = Kind::float_, .f = value});
	}
	// an empty str starting at the current end of the char pool; append
	// with str_push, the count is finalized from the pool size
	constexpr std::uint32_t make_str_here() {
		return push(Object{.kind = Kind::str, .first = static_cast<std::uint32_t>(a.chars.size())});
	}
	constexpr void str_push(std::uint32_t index, char unit) {
		a.chars.push_back(unit);
		++a.objs[index].count;
	}
	constexpr void str_append(std::uint32_t index, std::string_view part) {
		for (const char unit : part) {
			str_push(index, unit);
		}
	}
	constexpr std::uint32_t make_str(std::string_view value) {
		const std::uint32_t index = make_str_here();
		str_append(index, value);
		return index;
	}
	constexpr std::string_view str_of(const Object & object) const noexcept {
		return std::string_view{a.chars.data() + object.first, object.count};
	}
	constexpr std::string_view str_of(std::uint32_t index) const noexcept {
		return str_of(a.objs[index]);
	}

	// --- truthiness (Python bool(x)) --------------------------------------

	constexpr bool truthy(std::uint32_t index) const noexcept {
		const Object & object = a.objs[index];
		switch (object.kind) {
			case Kind::none: return false;
			case Kind::boolean:
			case Kind::int_: return object.i != 0;
			case Kind::float_: return object.f != 0.0;
			case Kind::str:
			case Kind::tuple:
			case Kind::list:
			case Kind::set:
			case Kind::dict: return object.count != 0;
			case Kind::range:
				return detail::range_len(a.objs[object.first].i,
				                         a.objs[object.first + 1].i,
				                         a.objs[object.first + 2].i) != 0;
			case Kind::function: return true;
		}
		return true;
	}

	// --- scopes -----------------------------------------------------------

	// enter/leave a call's locals frame; the binding pool truncates
	// back on leave, so locals never outlive their call (LIFO by
	// construction - v0.1 has no closures to keep them alive)
	constexpr void push_frame() {
		stack.push_back(Frame{static_cast<std::uint32_t>(a.frames.size()), 0});
	}
	constexpr void pop_frame() {
		a.frames.resize(stack.back().first);
		stack.pop_back();
	}

	// resolve a name: the innermost frame's locals first (there are no
	// closures - intermediate frames are invisible), then globals
	constexpr std::uint32_t lookup(std::string_view name) const noexcept {
		if (!stack.empty()) {
			const Frame & frame = stack.back();
			for (std::uint32_t at = frame.count; at > 0; --at) {
				if (a.frames[frame.first + at - 1].name == name) {
					return a.frames[frame.first + at - 1].obj;
				}
			}
		}
		for (std::uint32_t at = globals_count; at > 0; --at) {
			if (a.frames[at - 1].name == name) {
				return a.frames[at - 1].obj;
			}
		}
		return not_found;
	}

	// bind a name in the current scope (innermost frame, else globals)
	constexpr void bind(std::string_view name, std::uint32_t object) {
		if (!stack.empty()) {
			Frame & frame = stack.back();
			for (std::uint32_t at = frame.count; at > 0; --at) {
				if (a.frames[frame.first + at - 1].name == name) {
					a.frames[frame.first + at - 1].obj = object;
					return;
				}
			}
			a.frames.push_back(Binding{name, object});
			++frame.count;
			return;
		}
		for (std::uint32_t at = globals_count; at > 0; --at) {
			if (a.frames[at - 1].name == name) {
				a.frames[at - 1].obj = object;
				return;
			}
		}
		a.frames.push_back(Binding{name, object});
		++globals_count;
	}

	// --- the exception channel ---------------------------------------------

	// set the raised flag + PyError and hand back None so call sites can
	// `return st.raise_error(...)`. First raise wins; a later raise while
	// one is in flight is ignored (eval short-circuits anyway).
	constexpr std::uint32_t raise_error(ex_kind type, std::initializer_list<std::string_view> parts) noexcept {
		if (!raised) {
			raised = true;
			error = PyError{.type = type};
			for (const std::string_view part : parts) {
				error.append(part);
			}
		}
		return none_index;
	}
	constexpr std::uint32_t raise_error(ex_kind type, std::string_view message) noexcept {
		return raise_error(type, {message});
	}
};

} // namespace ctpy

#endif
