#ifndef CTPY__BUILTINS__HPP
#define CTPY__BUILTINS__HPP

#include "version.hpp"
#include "text.hpp"
#include "ast.hpp"
#include "object.hpp"
#include "eval.hpp"

#ifndef CTPY_IN_A_MODULE
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#endif

// The compile-time builtins and every call site's dispatch. The v0.1
// builtin set is complete here: print (sep/end keywords), len, range
// (lazy object - three pool ints, iterated arithmetically), sum, min,
// max (iterable and 2+-scalar forms), abs, str, int, bool, sorted
// (stable), enumerate, zip, open (the compile-time VFS seam), input
// (the mounted stdin) - plus the Python str()/repr() formatting every
// one of them and the f-string evaluator (fstring.hpp) share.
//
// Calling a NAME dispatches here: a bound name wins (Python lets you
// shadow builtins), an unbound one is looked up in the builtin table,
// anything else is a NameError. A bound FUNCTION object (made by a def
// statement, exec.hpp) is invoked through its type-erased thunk in
// State::thunks - this call site never sees the def's AST type.
// Calling an ATTRIBUTE (obj.method(...)) dispatches on the object's
// Kind plus the method name - an unknown pair is an AttributeError.
//
// Documented v0.1 deviations (each raises a soft error or is noted):
//   - floats print with up to 16 significant digits (trailing zeros
//     trimmed, CPython's fixed/scientific thresholds); CPython's
//     shortest-round-trip repr needs 17 digits for some values
//     (0.1 + 0.2 prints "0.3" here, "0.30000000000000004" there);
//   - enumerate() and zip() MATERIALIZE lists of tuples (CPython
//     returns lazy iterators; iteration, len() and indexing agree);
//   - keyword arguments exist only for print(sep=, end=): user calls
//     are positional-only, sum(start=)/sorted(key=, reverse=) raise;
//   - str() and int() are single-argument forms (no encoding, no
//     base); int() of an oversized value raises OverflowError where
//     CPython would grow a bignum;
//   - builtins are not first-class values (x = len is a NameError);
//   - zip() takes at most 16 iterables.

namespace ctpy {

namespace detail {

// === Python str() / repr() formatting =======================================

// --- float -> text (the "minimal repr" documented above) --------------------

constexpr double pow10d(int power) noexcept {
	constexpr double table[23] = {
		1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
		1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
	return table[power];
}

struct float_text {
	char data[40]{};
	std::size_t len = 0;

	constexpr void put(char unit) noexcept {
		data[len++] = unit;
	}
	constexpr void put(std::string_view part) noexcept {
		for (const char unit : part) {
			put(unit);
		}
	}
	constexpr std::string_view view() const noexcept {
		return std::string_view{data, len};
	}
};

// Python's float repr shape: fixed notation for 1e-4 <= |v| < 1e16,
// scientific outside, ".0" appended to integral fixed values, two-digit
// minimum exponent. Precision is 16 significant digits (see header).
constexpr float_text format_double(double value) noexcept {
	float_text out{};
	if (value != value) {
		out.put("nan");
		return out;
	}
	if ((std::bit_cast<std::uint64_t>(value) >> 63) != 0) {
		out.put('-');
		value = -value;
	}
	if (value > std::numeric_limits<double>::max()) {
		out.put("inf");
		return out;
	}
	if (value == 0.0) {
		out.put("0.0");
		return out;
	}
	int exp10 = 0;
	for (double m = value; m >= 10.0; m /= 10.0) {
		++exp10;
	}
	for (double m = value; m < 1.0; m *= 10.0) {
		--exp10;
	}
	// scale into a 16-digit integer (exact power-of-ten steps <= 1e22)
	double scaled = value;
	if (exp10 >= 16) {
		int steps = exp10 - 15;
		while (steps > 22) {
			scaled /= 1e22;
			steps -= 22;
		}
		scaled /= pow10d(steps);
	} else {
		int steps = 15 - exp10;
		while (steps > 22) {
			scaled *= 1e22;
			steps -= 22;
		}
		scaled *= pow10d(steps);
	}
	// round HALF-EVEN (naive +0.5 would double-round: the multiply above
	// already rounded 0.30000000000000004e16 up to a .5 boundary)
	unsigned long long mantissa = static_cast<unsigned long long>(scaled);
	const double fraction = scaled - static_cast<double>(mantissa);
	if (fraction > 0.5 || (fraction == 0.5 && (mantissa & 1ULL) != 0)) {
		++mantissa;
	}
	if (mantissa >= 10000000000000000ULL) {
		mantissa /= 10ULL;
		++exp10;
	}
	if (mantissa < 1000000000000000ULL) {
		mantissa *= 10ULL;
		--exp10;
	}
	char digit[16]{};
	for (int at = 15; at >= 0; --at) {
		digit[at] = static_cast<char>('0' + static_cast<char>(mantissa % 10ULL));
		mantissa /= 10ULL;
	}
	std::size_t ndigits = 16;
	while (ndigits > 1 && digit[ndigits - 1] == '0') {
		--ndigits;
	}
	if (exp10 >= 16 || exp10 < -4) { // scientific, like repr(1e16) == '1e+16'
		out.put(digit[0]);
		if (ndigits > 1) {
			out.put('.');
			for (std::size_t at = 1; at < ndigits; ++at) {
				out.put(digit[at]);
			}
		}
		out.put('e');
		out.put(exp10 < 0 ? '-' : '+');
		int magnitude = exp10 < 0 ? -exp10 : exp10;
		if (magnitude >= 100) {
			out.put(static_cast<char>('0' + static_cast<char>(magnitude / 100)));
			magnitude %= 100;
		}
		out.put(static_cast<char>('0' + static_cast<char>(magnitude / 10)));
		out.put(static_cast<char>('0' + static_cast<char>(magnitude % 10)));
	} else if (exp10 >= 0) { // fixed with the point inside or right of the digits
		for (int at = 0; at <= exp10; ++at) {
			out.put(static_cast<std::size_t>(at) < ndigits ? digit[at] : '0');
		}
		if (ndigits > static_cast<std::size_t>(exp10) + 1) {
			out.put('.');
			for (std::size_t at = static_cast<std::size_t>(exp10) + 1; at < ndigits; ++at) {
				out.put(digit[at]);
			}
		} else {
			out.put(".0");
		}
	} else { // fixed below one: 0.000ddd
		out.put("0.");
		for (int zero = 0; zero < -exp10 - 1; ++zero) {
			out.put('0');
		}
		for (std::size_t at = 0; at < ndigits; ++at) {
			out.put(digit[at]);
		}
	}
	return out;
}

// --- sinks: where the characters of str(x) go --------------------------------

// print() appends to the captured-stdout pool
template <typename St> struct stdout_sink {
	St & st;
	constexpr void push(char unit) {
		st.a.out.push_back(unit);
	}
};

// str() and f-strings append to an in-flight str object (which MUST be
// the last chars-pool user - write_object never allocates, so the run
// stays contiguous)
template <typename St> struct str_sink {
	St & st;
	std::uint32_t target;
	constexpr void push(char unit) {
		st.str_push(target, unit);
	}
};

template <typename Sink> constexpr void sink_text(Sink & sink, std::string_view part) {
	for (const char unit : part) {
		sink.push(unit);
	}
}

// repr of a str: CPython's quote choice (single, unless the content has
// a single quote and no double quote) and escapes (backslash, the quote,
// \n \r \t, \xHH for other control bytes)
template <typename St, typename Sink>
constexpr void write_str_repr(const St & st, Sink & sink, const Object & value) {
	const std::string_view content = st.str_of(value);
	bool has_single = false;
	bool has_double = false;
	for (const char unit : content) {
		has_single = has_single || unit == '\'';
		has_double = has_double || unit == '"';
	}
	const char quote = (has_single && !has_double) ? '"' : '\'';
	sink.push(quote);
	for (const char unit : content) {
		if (unit == '\\' || unit == quote) {
			sink.push('\\');
			sink.push(unit);
		} else if (unit == '\n') {
			sink_text(sink, "\\n");
		} else if (unit == '\r') {
			sink_text(sink, "\\r");
		} else if (unit == '\t') {
			sink_text(sink, "\\t");
		} else if (static_cast<unsigned char>(unit) < 0x20 || unit == '\x7f') {
			constexpr std::string_view hex = "0123456789abcdef";
			sink_text(sink, "\\x");
			sink.push(hex[static_cast<std::size_t>(static_cast<unsigned char>(unit) >> 4)]);
			sink.push(hex[static_cast<std::size_t>(static_cast<unsigned char>(unit) & 0xf)]);
		} else {
			sink.push(unit);
		}
	}
	sink.push(quote);
}

// Python str(x) (repr == false) / repr(x) (repr == true) of any object,
// recursing through containers (whose elements always print as reprs).
// ALLOCATES NOTHING: reads pools, pushes characters - so a str_sink's
// target run stays contiguous through the whole walk.
template <typename St, typename Sink>
constexpr void write_object(St & st, Sink & sink, std::uint32_t index, bool repr) {
	const Object object = st.a.objs[index]; // copy: the sink may grow the char pool
	switch (object.kind) {
		case Kind::none:
			sink_text(sink, "None");
			return;
		case Kind::boolean:
			sink_text(sink, object.i != 0 ? "True" : "False");
			return;
		case Kind::int_:
			sink_text(sink, dec(object.i).view());
			return;
		case Kind::float_:
			sink_text(sink, format_double(object.f).view());
			return;
		case Kind::str:
			if (repr) {
				write_str_repr(st, sink, object);
			} else {
				sink_text(sink, st.str_of(object));
			}
			return;
		case Kind::range: {
			const long long step = st.a.objs[object.first + 2].i;
			sink_text(sink, "range(");
			sink_text(sink, dec(st.a.objs[object.first].i).view());
			sink_text(sink, ", ");
			sink_text(sink, dec(st.a.objs[object.first + 1].i).view());
			if (step != 1) {
				sink_text(sink, ", ");
				sink_text(sink, dec(step).view());
			}
			sink.push(')');
			return;
		}
		case Kind::tuple: {
			sink.push('(');
			for (std::uint32_t at = 0; at < object.count; ++at) {
				if (at != 0) {
					sink_text(sink, ", ");
				}
				write_object(st, sink, object.first + at, true);
			}
			if (object.count == 1) {
				sink.push(',');
			}
			sink.push(')');
			return;
		}
		case Kind::list: {
			sink.push('[');
			for (std::uint32_t at = 0; at < object.count; ++at) {
				if (at != 0) {
					sink_text(sink, ", ");
				}
				write_object(st, sink, object.first + at, true);
			}
			sink.push(']');
			return;
		}
		case Kind::set: {
			if (object.count == 0) {
				sink_text(sink, "set()"); // unreachable from a display, kept for completeness
				return;
			}
			sink.push('{');
			for (std::uint32_t at = 0; at < object.count; ++at) {
				if (at != 0) {
					sink_text(sink, ", ");
				}
				write_object(st, sink, object.first + at, true);
			}
			sink.push('}');
			return;
		}
		case Kind::dict: {
			sink.push('{');
			for (std::uint32_t at = 0; at < object.count; ++at) {
				if (at != 0) {
					sink_text(sink, ", ");
				}
				const Pair entry = st.a.pairs[object.first + at];
				write_object(st, sink, entry.key, true);
				sink_text(sink, ": ");
				write_object(st, sink, entry.value, true);
			}
			sink.push('}');
			return;
		}
		case Kind::function:
			sink_text(sink, "<function>"); // CPython adds a name and an address
			return;
		case Kind::file:
			sink_text(sink, "<TextIOWrapper>"); // CPython adds name/mode/encoding
			return;
	}
}

// === iteration support shared by the sequence-consuming builtins =============

// push contiguous COPIES of every element of `sequence` (a caller-held
// copy) and return the run's first slot: str shares the char pool
// (per-character sub-views), range synthesizes its ints, dict yields
// its keys in insertion order, tuple/list/set copy their runs
template <typename St> constexpr std::uint32_t materialize_run(St & st, const Object & sequence) {
	const std::uint32_t first = static_cast<std::uint32_t>(st.a.objs.size());
	const long long total = iter_len(st, sequence);
	switch (sequence.kind) {
		case Kind::str:
			for (long long at = 0; at < total; ++at) {
				st.a.objs.push_back(Object{.kind = Kind::str,
				                           .first = sequence.first + static_cast<std::uint32_t>(at),
				                           .count = 1});
			}
			break;
		case Kind::range: {
			const long long start = st.a.objs[sequence.first].i;
			const long long step = st.a.objs[sequence.first + 2].i;
			for (long long at = 0; at < total; ++at) {
				st.a.objs.push_back(Object{.kind = Kind::int_, .i = start + at * step});
			}
			break;
		}
		case Kind::dict:
			for (long long at = 0; at < total; ++at) {
				st.a.objs.push_back(
					st.a.objs[st.a.pairs[sequence.first + static_cast<std::uint32_t>(at)].key]);
			}
			break;
		default: // tuple/list/set: contiguous already, copy the run
			for (long long at = 0; at < total; ++at) {
				st.a.objs.push_back(st.a.objs[sequence.first + static_cast<std::uint32_t>(at)]);
			}
			break;
	}
	return first;
}

// === the builtins ============================================================

// a keyword argument as passed to a builtin (only print() takes any)
struct kw_pass {
	std::string_view name{};
	std::uint32_t value = 0;
};

// --- print(*args, sep=' ', end='\n') -> captured stdout ----------------------

template <typename St>
constexpr std::uint32_t builtin_print(St & st, const std::uint32_t * argv, std::size_t argc,
                                      const kw_pass * kwv, std::size_t kwc) {
	std::string_view sep = " ";
	std::string_view end = "\n";
	for (std::size_t at = 0; at < kwc; ++at) {
		if (kwv[at].name != "sep" && kwv[at].name != "end") {
			return st.raise_error(ex_kind::TypeError,
				{"print() got an unexpected keyword argument '", kwv[at].name, "'"});
		}
		const Object & option = st.a.objs[kwv[at].value];
		if (option.kind == Kind::none) {
			continue; // None means the default, per CPython
		}
		if (option.kind != Kind::str) {
			return st.raise_error(ex_kind::TypeError,
				{kwv[at].name, " must be None or a string, not ", type_name(option.kind)});
		}
		(kwv[at].name == "sep" ? sep : end) = st.str_of(option);
	}
	stdout_sink<St> sink{st};
	for (std::size_t at = 0; at < argc; ++at) {
		if (at != 0) {
			sink_text(sink, sep);
		}
		write_object(st, sink, argv[at], false);
	}
	sink_text(sink, end);
	return st.none();
}

// --- range() -----------------------------------------------------------------

template <typename St>
constexpr std::uint32_t builtin_range(St & st, const std::uint32_t * argv, std::size_t argc) {
	if (argc == 0) {
		return st.raise_error(ex_kind::TypeError, "range expected at least 1 argument, got 0");
	}
	if (argc > 3) {
		const auto got = dec(static_cast<long long>(argc));
		return st.raise_error(ex_kind::TypeError,
			{"range expected at most 3 arguments, got ", got.view()});
	}
	long long parts[3]{};
	for (std::size_t at = 0; at < argc; ++at) {
		const Object & argument = st.a.objs[argv[at]];
		if (!is_int_like(argument.kind)) {
			return st.raise_error(ex_kind::TypeError,
				{"'", type_name(argument.kind), "' object cannot be interpreted as an integer"});
		}
		parts[at] = argument.i;
	}
	const long long start = argc >= 2 ? parts[0] : 0;
	const long long stop = argc >= 2 ? parts[1] : parts[0];
	const long long step = argc == 3 ? parts[2] : 1;
	if (step == 0) {
		return st.raise_error(ex_kind::ValueError, "range() arg 3 must not be zero");
	}
	return make_range(st, start, stop, step);
}

// --- len() ---------------------------------------------------------------------

template <typename St>
constexpr std::uint32_t builtin_len(St & st, const std::uint32_t * argv, std::size_t argc) {
	if (argc != 1) {
		const auto got = dec(static_cast<long long>(argc));
		return st.raise_error(ex_kind::TypeError,
			{"len() takes exactly one argument (", got.view(), " given)"});
	}
	const Object & object = st.a.objs[argv[0]];
	if (!iterable_kind(object.kind)) {
		return st.raise_error(ex_kind::TypeError,
			{"object of type '", type_name(object.kind), "' has no len()"});
	}
	return st.make_int(iter_len(st, object));
}

// --- sum(iterable[, start]) ------------------------------------------------------

template <typename St>
constexpr std::uint32_t builtin_sum(St & st, const std::uint32_t * argv, std::size_t argc) {
	if (argc == 0) {
		return st.raise_error(ex_kind::TypeError, "sum expected at least 1 argument, got 0");
	}
	if (argc > 2) {
		const auto got = dec(static_cast<long long>(argc));
		return st.raise_error(ex_kind::TypeError,
			{"sum expected at most 2 arguments, got ", got.view()});
	}
	const Object sequence = st.a.objs[argv[0]]; // copy: the loop below allocates
	if (!iterable_kind(sequence.kind)) {
		return st.raise_error(ex_kind::TypeError,
			{"'", type_name(sequence.kind), "' object is not iterable"});
	}
	std::uint32_t acc = 0;
	if (argc == 2) {
		if (st.a.objs[argv[1]].kind == Kind::str) {
			return st.raise_error(ex_kind::TypeError,
				"sum() can't sum strings [use ''.join(seq) instead]");
		}
		acc = argv[1];
	} else {
		acc = st.make_int(0);
	}
	const long long total = iter_len(st, sequence);
	for (long long at = 0; at < total; ++at) {
		const std::uint32_t element = iter_get(st, sequence, at);
		acc = bin_op(st, bop::add, acc, element);
		if (st.raised) {
			return st.none();
		}
	}
	return acc;
}

// --- min()/max(): iterable form and 2+-scalar form --------------------------------

template <typename St>
constexpr std::uint32_t builtin_min_max(St & st, const std::uint32_t * argv, std::size_t argc,
                                        bool is_min) {
	const std::string_view name = is_min ? "min" : "max";
	const cop wins = is_min ? cop::lt : cop::gt;
	if (argc == 0) {
		return st.raise_error(ex_kind::TypeError, {name, " expected at least 1 argument, got 0"});
	}
	if (argc == 1) {
		const Object sequence = st.a.objs[argv[0]]; // copy: iter_get may allocate
		if (!iterable_kind(sequence.kind)) {
			return st.raise_error(ex_kind::TypeError,
				{"'", type_name(sequence.kind), "' object is not iterable"});
		}
		const long long total = iter_len(st, sequence);
		if (total == 0) {
			return st.raise_error(ex_kind::ValueError, {name, "() iterable argument is empty"});
		}
		std::uint32_t best = iter_get(st, sequence, 0);
		for (long long at = 1; at < total; ++at) {
			const std::uint32_t challenger = iter_get(st, sequence, at);
			const bool won = compare_op(st, wins, challenger, best);
			if (st.raised) {
				return st.none();
			}
			if (won) {
				best = challenger;
			}
		}
		return best;
	}
	std::uint32_t best = argv[0];
	for (std::size_t at = 1; at < argc; ++at) {
		const bool won = compare_op(st, wins, argv[at], best);
		if (st.raised) {
			return st.none();
		}
		if (won) {
			best = argv[at];
		}
	}
	return best;
}

// --- abs() --------------------------------------------------------------------------

template <typename St>
constexpr std::uint32_t builtin_abs(St & st, const std::uint32_t * argv, std::size_t argc) {
	if (argc != 1) {
		const auto got = dec(static_cast<long long>(argc));
		return st.raise_error(ex_kind::TypeError,
			{"abs() takes exactly one argument (", got.view(), " given)"});
	}
	const Object & value = st.a.objs[argv[0]];
	if (value.kind == Kind::float_) {
		// the sign BIT, so abs(-0.0) is 0.0 like CPython's
		return st.make_float((std::bit_cast<std::uint64_t>(value.f) >> 63) != 0 ? -value.f : value.f);
	}
	if (is_int_like(value.kind)) {
		if (value.i == std::numeric_limits<long long>::min()) {
			return st.raise_error(ex_kind::OverflowError,
				"ctpy: abs() result does not fit the 64-bit v0.1 int");
		}
		return st.make_int(value.i < 0 ? -value.i : value.i);
	}
	return st.raise_error(ex_kind::TypeError,
		{"bad operand type for abs(): '", type_name(value.kind), "'"});
}

// --- str() ---------------------------------------------------------------------------

template <typename St>
constexpr std::uint32_t builtin_str(St & st, const std::uint32_t * argv, std::size_t argc) {
	if (argc == 0) {
		return st.make_str("");
	}
	if (argc > 1) {
		return st.raise_error(ex_kind::TypeError,
			"ctpy v0.1: str() takes at most one argument (no encoding form)");
	}
	if (st.a.objs[argv[0]].kind == Kind::str) {
		return argv[0]; // strs are immutable: share the object
	}
	const std::uint32_t out = st.make_str_here();
	str_sink<St> sink{st, out};
	write_object(st, sink, argv[0], false);
	return out;
}

// --- int() -----------------------------------------------------------------------------

template <typename St>
constexpr std::uint32_t builtin_int(St & st, const std::uint32_t * argv, std::size_t argc) {
	if (argc == 0) {
		return st.make_int(0);
	}
	if (argc > 1) {
		return st.raise_error(ex_kind::TypeError,
			"ctpy v0.1: int() takes at most one argument (no base form)");
	}
	const Object & value = st.a.objs[argv[0]];
	if (is_int_like(value.kind)) {
		return st.make_int(value.i);
	}
	if (value.kind == Kind::float_) {
		if (value.f != value.f) {
			return st.raise_error(ex_kind::ValueError, "cannot convert float NaN to integer");
		}
		const double magnitude = value.f < 0.0 ? -value.f : value.f;
		if (magnitude > std::numeric_limits<double>::max()) {
			return st.raise_error(ex_kind::OverflowError, "cannot convert float infinity to integer");
		}
		if (magnitude >= 9223372036854775808.0) {
			return st.raise_error(ex_kind::OverflowError,
				"ctpy: int() result does not fit the 64-bit v0.1 int");
		}
		return st.make_int(static_cast<long long>(value.f)); // truncates toward zero
	}
	if (value.kind == Kind::str) {
		const std::string_view content = st.str_of(value);
		std::size_t from = 0;
		std::size_t to = content.size();
		constexpr auto is_space = [](char unit) {
			return unit == ' ' || unit == '\t' || unit == '\n' || unit == '\r' ||
			       unit == '\v' || unit == '\f';
		};
		while (from < to && is_space(content[from])) {
			++from;
		}
		while (to > from && is_space(content[to - 1])) {
			--to;
		}
		bool negative = false;
		if (from < to && (content[from] == '+' || content[from] == '-')) {
			negative = content[from] == '-';
			++from;
		}
		bool valid = from < to;
		unsigned long long magnitude = 0;
		const unsigned long long limit =
			negative ? 9223372036854775808ULL : 9223372036854775807ULL;
		for (std::size_t at = from; at < to && valid; ++at) {
			const char unit = content[at];
			if (unit < '0' || unit > '9') {
				valid = false;
				break;
			}
			const unsigned long long digit = static_cast<unsigned long long>(unit - '0');
			if (magnitude > (limit - digit) / 10ULL) {
				return st.raise_error(ex_kind::OverflowError,
					"ctpy: int() result does not fit the 64-bit v0.1 int");
			}
			magnitude = magnitude * 10ULL + digit;
		}
		if (!valid) {
			st.raise_error(ex_kind::ValueError, "invalid literal for int() with base 10: ");
			append_repr(st, argv[0]);
			return st.none();
		}
		return st.make_int(negative ? static_cast<long long>(0ULL - magnitude)
		                            : static_cast<long long>(magnitude));
	}
	return st.raise_error(ex_kind::TypeError,
		{"int() argument must be a string, a bytes-like object or a real number, not '",
		 type_name(value.kind), "'"});
}

// --- bool() --------------------------------------------------------------------------------

template <typename St>
constexpr std::uint32_t builtin_bool(St & st, const std::uint32_t * argv, std::size_t argc) {
	if (argc == 0) {
		return st.make_bool(false);
	}
	if (argc > 1) {
		const auto got = dec(static_cast<long long>(argc));
		return st.raise_error(ex_kind::TypeError,
			{"bool expected at most 1 argument, got ", got.view()});
	}
	return st.make_bool(st.truthy(argv[0]));
}

// --- sorted(): stable, always a fresh list ---------------------------------------------------

template <typename St>
constexpr std::uint32_t builtin_sorted(St & st, const std::uint32_t * argv, std::size_t argc) {
	if (argc != 1) {
		const auto got = dec(static_cast<long long>(argc));
		return st.raise_error(ex_kind::TypeError,
			{"sorted expected 1 argument, got ", got.view()});
	}
	const Object sequence = st.a.objs[argv[0]]; // copy: materializing allocates
	if (!iterable_kind(sequence.kind)) {
		return st.raise_error(ex_kind::TypeError,
			{"'", type_name(sequence.kind), "' object is not iterable"});
	}
	const long long total = iter_len(st, sequence);
	const std::uint32_t first = materialize_run(st, sequence);
	// insertion sort: stable (strict-< bubbling stops at the first equal),
	// O(n^2) compares - fine at compile-time scale; the comparisons
	// themselves never allocate, so the run stays the pool's tail
	for (long long at = 1; at < total; ++at) {
		for (long long into = at; into > 0; --into) {
			const std::uint32_t here = first + static_cast<std::uint32_t>(into);
			const bool less = compare_op(st, cop::lt, here, here - 1);
			if (st.raised) {
				return st.none();
			}
			if (!less) {
				break;
			}
			const Object shuffled = st.a.objs[here];
			st.a.objs[here] = st.a.objs[here - 1];
			st.a.objs[here - 1] = shuffled;
		}
	}
	return st.push(Object{.kind = Kind::list,
	                      .first = first,
	                      .count = static_cast<std::uint32_t>(total)});
}

// --- enumerate(iterable[, start]): a MATERIALIZED list of (index, value) ----------------------

template <typename St>
constexpr std::uint32_t builtin_enumerate(St & st, const std::uint32_t * argv, std::size_t argc) {
	if (argc == 0) {
		return st.raise_error(ex_kind::TypeError, "enumerate expected at least 1 argument, got 0");
	}
	if (argc > 2) {
		const auto got = dec(static_cast<long long>(argc));
		return st.raise_error(ex_kind::TypeError,
			{"enumerate() takes at most 2 arguments (", got.view(), " given)"});
	}
	long long start = 0;
	if (argc == 2) {
		const Object & from = st.a.objs[argv[1]];
		if (!is_int_like(from.kind)) {
			return st.raise_error(ex_kind::TypeError,
				{"'", type_name(from.kind), "' object cannot be interpreted as an integer"});
		}
		start = from.i;
	}
	const Object sequence = st.a.objs[argv[0]]; // copy: materializing allocates
	if (!iterable_kind(sequence.kind)) {
		return st.raise_error(ex_kind::TypeError,
			{"'", type_name(sequence.kind), "' object is not iterable"});
	}
	const long long total = iter_len(st, sequence);
	const std::uint32_t elements = materialize_run(st, sequence);
	// each (index, element) pair is a contiguous 2-run; nothing else
	// allocates between the two pushes
	const std::uint32_t pairs = static_cast<std::uint32_t>(st.a.objs.size());
	for (long long at = 0; at < total; ++at) {
		st.a.objs.push_back(Object{.kind = Kind::int_, .i = start + at});
		st.a.objs.push_back(st.a.objs[elements + static_cast<std::uint32_t>(at)]);
	}
	const std::uint32_t headers = static_cast<std::uint32_t>(st.a.objs.size());
	for (long long at = 0; at < total; ++at) {
		st.a.objs.push_back(Object{.kind = Kind::tuple,
		                           .first = pairs + 2 * static_cast<std::uint32_t>(at),
		                           .count = 2});
	}
	return st.push(Object{.kind = Kind::list,
	                      .first = headers,
	                      .count = static_cast<std::uint32_t>(total)});
}

// --- zip(*iterables): shortest, a MATERIALIZED list of tuples ----------------------------------

inline constexpr std::size_t zip_max_iterables = 16;

template <typename St>
constexpr std::uint32_t builtin_zip(St & st, const std::uint32_t * argv, std::size_t argc) {
	if (argc > zip_max_iterables) {
		return st.raise_error(ex_kind::TypeError, "ctpy v0.1: zip() takes at most 16 iterables");
	}
	Object sequences[zip_max_iterables]{};
	long long total = 0;
	for (std::size_t at = 0; at < argc; ++at) {
		sequences[at] = st.a.objs[argv[at]]; // copies: materializing allocates
		if (!iterable_kind(sequences[at].kind)) {
			return st.raise_error(ex_kind::TypeError,
				{"'", type_name(sequences[at].kind), "' object is not iterable"});
		}
		const long long len = iter_len(st, sequences[at]);
		if (at == 0 || len < total) {
			total = len; // shortest input wins
		}
	}
	std::uint32_t runs[zip_max_iterables]{};
	for (std::size_t at = 0; at < argc; ++at) {
		runs[at] = materialize_run(st, sequences[at]);
	}
	// row-major element block, then the tuple headers, then the list -
	// each row [rows + i*argc, ...) is one tuple's contiguous run
	const std::uint32_t rows = static_cast<std::uint32_t>(st.a.objs.size());
	for (long long row = 0; row < total; ++row) {
		for (std::size_t column = 0; column < argc; ++column) {
			st.a.objs.push_back(st.a.objs[runs[column] + static_cast<std::uint32_t>(row)]);
		}
	}
	const std::uint32_t headers = static_cast<std::uint32_t>(st.a.objs.size());
	for (long long row = 0; row < total; ++row) {
		st.a.objs.push_back(Object{
			.kind = Kind::tuple,
			.first = rows + static_cast<std::uint32_t>(row) * static_cast<std::uint32_t>(argc),
			.count = static_cast<std::uint32_t>(argc)});
	}
	return st.push(Object{.kind = Kind::list,
	                      .first = headers,
	                      .count = static_cast<std::uint32_t>(total)});
}

// --- open(): the compile-time VFS (the std::embed-ready IO seam) --------------

// There is no filesystem at compile time, so open() resolves against
// the descriptors run() was given: ctpy::file<"path", "contents">
// (bind.hpp) seeds State::vfs before the module executes. A mounted
// path opens as a file object whose contents share the char pool; an
// unmounted one raises OSError the way CPython's FileNotFoundError
// (an OSError subclass) spells it. v0.1 is read-only and text-mode:
// only the one-argument form exists.
template <typename St>
constexpr std::uint32_t builtin_open(St & st, const std::uint32_t * argv, std::size_t argc) {
	if (argc != 1) {
		return st.raise_error(ex_kind::TypeError,
			"ctpy v0.1: open() takes exactly one argument (the compile-time VFS is read-only text)");
	}
	const Object path = st.a.objs[argv[0]]; // copy: make_str below grows the pool
	if (path.kind != Kind::str) {
		return st.raise_error(ex_kind::TypeError,
			{"expected str, bytes or os.PathLike object, not ", type_name(path.kind)});
	}
	for (std::size_t at = 0; at < st.vfs.size(); ++at) {
		if (st.vfs[at].path == st.str_of(path)) {
			// the file object holds its contents as a char-pool run;
			// read() hands the same run out as a str (no copy)
			const std::uint32_t out = st.make_str(st.vfs[at].contents);
			st.a.objs[out].kind = Kind::file;
			return out;
		}
	}
	st.raise_error(ex_kind::OSError, {});
	st.error.append("[Errno 2] No such file or directory: ");
	append_repr(st, argv[0]);
	return st.none();
}

// --- input(): reads the mounted ctpy::stdin_text<> line by line ----------------

template <typename St>
constexpr std::uint32_t builtin_input(St & st, const std::uint32_t * argv, std::size_t argc) {
	if (argc > 1) {
		const auto got = dec(static_cast<long long>(argc));
		return st.raise_error(ex_kind::TypeError,
			{"input expected at most 1 argument, got ", got.view()});
	}
	if (argc == 1) { // the prompt prints to captured stdout, no newline
		stdout_sink<St> sink{st};
		write_object(st, sink, argv[0], false);
	}
	if (st.stdin_at >= st.stdin_content.size()) {
		return st.raise_error(ex_kind::EOFError, "EOF when reading a line");
	}
	const std::size_t from = st.stdin_at;
	std::size_t to = from;
	while (to < st.stdin_content.size() && st.stdin_content[to] != '\n') {
		++to;
	}
	// the newline is consumed but not part of the line, per CPython
	st.stdin_at = to < st.stdin_content.size() ? to + 1 : to;
	return st.make_str(st.stdin_content.substr(from, to - from));
}

// --- the builtin table ---------------------------------------------------------

constexpr bool is_builtin(std::string_view name) noexcept {
	return name == "print" || name == "range" || name == "len" || name == "sum" ||
	       name == "min" || name == "max" || name == "abs" || name == "str" ||
	       name == "int" || name == "bool" || name == "sorted" || name == "enumerate" ||
	       name == "zip" || name == "open" || name == "input";
}

template <typename St>
constexpr std::uint32_t call_builtin(St & st, std::string_view name,
                                     const std::uint32_t * argv, std::size_t argc,
                                     const kw_pass * kwv, std::size_t kwc) {
	if (name == "print") {
		return builtin_print(st, argv, argc, kwv, kwc);
	}
	if (kwc != 0) {
		// only print() takes keywords in v0.1 (CPython's sum(start=),
		// sorted(key=/reverse=), min/max(key=/default=) raise here)
		return st.raise_error(ex_kind::TypeError, {name, "() takes no keyword arguments"});
	}
	if (name == "range") {
		return builtin_range(st, argv, argc);
	}
	if (name == "len") {
		return builtin_len(st, argv, argc);
	}
	if (name == "sum") {
		return builtin_sum(st, argv, argc);
	}
	if (name == "min") {
		return builtin_min_max(st, argv, argc, true);
	}
	if (name == "max") {
		return builtin_min_max(st, argv, argc, false);
	}
	if (name == "abs") {
		return builtin_abs(st, argv, argc);
	}
	if (name == "str") {
		return builtin_str(st, argv, argc);
	}
	if (name == "int") {
		return builtin_int(st, argv, argc);
	}
	if (name == "bool") {
		return builtin_bool(st, argv, argc);
	}
	if (name == "sorted") {
		return builtin_sorted(st, argv, argc);
	}
	if (name == "enumerate") {
		return builtin_enumerate(st, argv, argc);
	}
	if (name == "zip") {
		return builtin_zip(st, argv, argc);
	}
	if (name == "open") {
		return builtin_open(st, argv, argc);
	}
	if (name == "input") {
		return builtin_input(st, argv, argc);
	}
	return st.raise_error(ex_kind::NameError, {"name '", name, "' is not defined"});
}

// --- methods (the minimal v0.1 set) --------------------------------------------

// dict.keys()/.values()/.items() return materialized LISTS (v0.1 has no
// view objects; CPython code that iterates or len()s them works the
// same). items() lays out the element runs first (key/value copies,
// pairwise), then the tuple headers as one contiguous run - which IS
// the list's element run.
template <typename St>
constexpr std::uint32_t dict_view_list(St & st, const Object & self, int which) {
	if (which == 2) {
		const std::uint32_t base = static_cast<std::uint32_t>(st.a.objs.size());
		for (std::uint32_t at = 0; at < self.count; ++at) {
			const Pair entry = st.a.pairs[self.first + at];
			st.a.objs.push_back(st.a.objs[entry.key]);
			st.a.objs.push_back(st.a.objs[entry.value]);
		}
		const std::uint32_t headers = static_cast<std::uint32_t>(st.a.objs.size());
		for (std::uint32_t at = 0; at < self.count; ++at) {
			st.a.objs.push_back(Object{.kind = Kind::tuple, .first = base + 2 * at, .count = 2});
		}
		return st.push(Object{.kind = Kind::list, .first = headers, .count = self.count});
	}
	const std::uint32_t first = static_cast<std::uint32_t>(st.a.objs.size());
	for (std::uint32_t at = 0; at < self.count; ++at) {
		const Pair entry = st.a.pairs[self.first + at];
		st.a.objs.push_back(st.a.objs[which == 0 ? entry.key : entry.value]);
	}
	return st.push(Object{.kind = Kind::list, .first = first, .count = self.count});
}

template <typename St>
constexpr std::uint32_t call_method(St & st, std::uint32_t self, std::string_view name,
                                    const std::uint32_t * argv, std::size_t argc) {
	const Object object = st.a.objs[self]; // copy: methods below may grow the pool
	if (object.kind == Kind::file && name == "read") {
		if (argc != 0) {
			return st.raise_error(ex_kind::TypeError,
				"ctpy v0.1: read() takes no arguments (the whole file is read at once)");
		}
		if (st.a.objs[self].i != 0) { // already consumed: CPython returns ''
			return st.make_str("");
		}
		st.a.objs[self].i = 1;
		// share the file's char run - str objects are immutable
		return st.push(Object{.kind = Kind::str, .first = object.first, .count = object.count});
	}
	if (object.kind == Kind::list && name == "append") {
		if (argc != 1) {
			const auto got = dec(static_cast<long long>(argc));
			return st.raise_error(ex_kind::TypeError,
				{"list.append() takes exactly one argument (", got.view(), " given)"});
		}
		list_append(st, self, argv[0]);
		return st.none();
	}
	if (object.kind == Kind::dict) {
		const int which = name == "keys" ? 0 : name == "values" ? 1 : name == "items" ? 2 : -1;
		if (which >= 0) {
			if (argc != 0) {
				const auto got = dec(static_cast<long long>(argc));
				return st.raise_error(ex_kind::TypeError,
					{name, "() takes no arguments (", got.view(), " given)"});
			}
			return dict_view_list(st, object, which);
		}
		if (name == "get") {
			if (argc < 1) {
				return st.raise_error(ex_kind::TypeError, "get expected at least 1 argument, got 0");
			}
			if (argc > 2) {
				const auto got = dec(static_cast<long long>(argc));
				return st.raise_error(ex_kind::TypeError,
					{"get expected at most 2 arguments, got ", got.view()});
			}
			if (!hashable(st, argv[0])) {
				return st.raise_error(ex_kind::TypeError,
					{"unhashable type: '", type_name(st.a.objs[argv[0]].kind), "'"});
			}
			for (std::uint32_t at = 0; at < object.count; ++at) {
				if (object_eq(st, argv[0], st.a.pairs[object.first + at].key)) {
					return st.a.pairs[object.first + at].value;
				}
			}
			return argc == 2 ? argv[1] : st.none();
		}
	}
	return st.raise_error(ex_kind::AttributeError,
		{"'", type_name(object.kind), "' object has no attribute '", name, "'"});
}

// --- evaluating a call's arguments (positional and keyword, in source order) ---

template <typename A> struct call_arg {
	template <typename St>
	static constexpr bool run(St & st, std::uint32_t * argv, std::size_t & argc,
	                          kw_pass *, std::size_t &) {
		argv[argc] = eval_node<A, St>(st);
		++argc;
		return !st.raised;
	}
};
template <typename N, typename V> struct call_arg<ast::kwarg<N, V>> {
	template <typename St>
	static constexpr bool run(St & st, std::uint32_t *, std::size_t &,
	                          kw_pass * kwv, std::size_t & kwc) {
		kwv[kwc] = kw_pass{N::view(), eval_node<V, St>(st)};
		++kwc;
		return !st.raised;
	}
};

// --- calling a name -------------------------------------------------------------

template <typename Text, typename... Args>
struct evaluator<ast::call_expr<ast::name<Text>, Args...>> {
	template <typename St> static constexpr std::uint32_t run(St & st) {
		const std::uint32_t bound = st.lookup(Text::view());
		if (bound != not_found && st.a.objs[bound].kind != Kind::function) {
			// a binding shadows any builtin, and only function objects
			// (and builtins) are callable in the v0.1 subset
			return st.raise_error(ex_kind::TypeError,
				{"'", type_name(st.a.objs[bound].kind), "' object is not callable"});
		}
		if (bound == not_found && !is_builtin(Text::view())) {
			return st.raise_error(ex_kind::NameError,
				{"name '", Text::view(), "' is not defined"});
		}
		// copy the callee OBJECT before the arguments grow the pool
		const Object fn = bound != not_found ? st.a.objs[bound] : Object{};
		std::uint32_t argv[sizeof...(Args) + 1]{};
		kw_pass kwv[sizeof...(Args) + 1]{};
		std::size_t argc = 0;
		std::size_t kwc = 0;
		const bool complete =
			(call_arg<Args>::template run<St>(st, argv, argc, kwv, kwc) && ...);
		(void)complete;
		if (st.raised) {
			return st.none();
		}
		if (bound != not_found) {
			if (kwc != 0) {
				return st.raise_error(ex_kind::TypeError,
					{Text::view(),
					 "() takes no keyword arguments (ctpy v0.1: calls are positional-only)"});
			}
			// a def'd function: dispatch through its type-erased thunk
			return st.thunks[static_cast<std::size_t>(fn.i)](st, fn, argv, argc);
		}
		return call_builtin(st, Text::view(), argv, argc, kwv, kwc);
	}
};

// --- calling a method: obj.name(args...) --------------------------------------

template <typename Obj, typename NameText, typename... Args>
struct evaluator<ast::call_expr<ast::attribute_expr<Obj, NameText>, Args...>> {
	template <typename St> static constexpr std::uint32_t run(St & st) {
		const std::uint32_t self = eval_node<Obj, St>(st);
		if (st.raised) {
			return st.none();
		}
		if constexpr ((is_kwarg<Args> || ...)) {
			return st.raise_error(ex_kind::TypeError,
				{NameText::view(), "() takes no keyword arguments (ctpy v0.1: calls are positional-only)"});
		} else {
			std::uint32_t argv[sizeof...(Args) + 1]{};
			std::size_t at = 0;
			const bool complete = ((argv[at++] = eval_node<Args, St>(st), !st.raised) && ...);
			(void)complete;
			(void)at;
			if (st.raised) {
				return st.none();
			}
			return call_method(st, self, NameText::view(), argv, sizeof...(Args));
		}
	}
};

} // namespace detail

} // namespace ctpy

#endif
