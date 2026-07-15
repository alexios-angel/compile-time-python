#ifndef CTPY__EVAL__HPP
#define CTPY__EVAL__HPP

#include "version.hpp"
#include "text.hpp"
#include "ast.hpp"
#include "object.hpp"
#include "parse.hpp"

#ifndef CTPY_IN_A_MODULE
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>
#endif

// Expression evaluation: a value-level tree-walk over the type-level
// AST. eval<Node>(state) returns the OBJECT-POOL INDEX of the result;
// a Python exception sets state.raised + state.error and every
// subsequent step short-circuits to None (NEVER C++ exceptions).
// Statement execution (exec<Stmt> and the Flow signals) lives in
// exec.hpp; builtin calls (range for now) dispatch in builtins.hpp.
//
// Python semantics implemented here, deliberately:
//   - `/` is true division (always float), `//` floors, `%` takes the
//     divisor's sign, `**` is right-associative and a negative integer
//     exponent produces a float;
//   - `or`/`and` short-circuit AND return the deciding OPERAND, not a
//     bool; chained comparisons evaluate each operand once and
//     short-circuit on the first failing link;
//   - `==` is deep structural equality (numeric kinds compare by
//     value: True == 1 == 1.0), ordering across unrelated types is a
//     TypeError;
//   - str supports + (concat), * int (repeat), `in` (substring),
//     ordering (lexicographic);
//   - literal texts arrive raw from the AST: escape cooking and
//     int/float spelling parses happen here, where value-level work
//     is cheap.
//
// v0.1 bounds (documented deviations): ints are 64-bit two's
// complement - `**` and `<<` raise OverflowError where CPython would
// grow a bignum, and a non-integral float exponent raises TypeError
// (no constexpr pow/exp/log in C++20).

namespace ctpy {

namespace detail {

// --- numeric helpers ------------------------------------------------------

constexpr double floor_d(double value) noexcept {
	const double truncated = static_cast<double>(static_cast<long long>(value));
	return truncated > value ? truncated - 1.0 : truncated;
}

// C++ / and % truncate toward zero; Python floors and gives the
// remainder the divisor's sign
constexpr long long floordiv_ll(long long lhs, long long rhs) noexcept {
	long long quotient = lhs / rhs;
	if (lhs % rhs != 0 && ((lhs < 0) != (rhs < 0))) {
		--quotient;
	}
	return quotient;
}
constexpr long long mod_ll(long long lhs, long long rhs) noexcept {
	long long remainder = lhs % rhs;
	if (remainder != 0 && ((remainder < 0) != (rhs < 0))) {
		remainder += rhs;
	}
	return remainder;
}
constexpr double mod_d(double lhs, double rhs) noexcept {
	return lhs - floor_d(lhs / rhs) * rhs;
}

constexpr bool mul_overflows(long long lhs, long long rhs) noexcept {
	constexpr long long hi = std::numeric_limits<long long>::max();
	constexpr long long lo = std::numeric_limits<long long>::min();
	if (lhs == 0 || rhs == 0) {
		return false;
	}
	if (lhs > 0) {
		return rhs > 0 ? lhs > hi / rhs : rhs < lo / lhs;
	}
	return rhs > 0 ? lhs < lo / rhs : lhs < hi / rhs;
}

// base ** exp for an INTEGRAL exp by squaring (exp may be negative)
constexpr double ipow_d(double base, long long exp) noexcept {
	const bool negative = exp < 0;
	unsigned long long magnitude =
		negative ? 0ULL - static_cast<unsigned long long>(exp) : static_cast<unsigned long long>(exp);
	double result = 1.0;
	while (magnitude != 0) {
		if ((magnitude & 1ULL) != 0) {
			result *= base;
		}
		magnitude >>= 1;
		if (magnitude != 0) {
			base *= base;
		}
	}
	return negative ? 1.0 / result : result;
}

// --- literal spellings (the AST carries raw source text) -------------------

constexpr long long parse_int(std::string_view text) noexcept {
	long long value = 0;
	for (const char digit : text) {
		value = value * 10 + (digit - '0');
	}
	return value;
}

constexpr double parse_float(std::string_view text) noexcept {
	double value = 0.0;
	std::size_t at = 0;
	for (; at < text.size() && text[at] != '.'; ++at) {
		value = value * 10.0 + static_cast<double>(text[at] - '0');
	}
	double scale = 0.1;
	for (++at; at < text.size(); ++at) {
		value += static_cast<double>(text[at] - '0') * scale;
		scale *= 0.1;
	}
	return value;
}

// cook a raw str_lit body into the char pool (escape processing
// happens here, once, at evaluation - the parse kept bodies verbatim)
template <typename St> constexpr std::uint32_t make_cooked_str(St & st, std::string_view raw) {
	const std::uint32_t out = st.make_str_here();
	for (std::size_t at = 0; at < raw.size(); ++at) {
		const char unit = raw[at];
		if (unit != '\\' || at + 1 == raw.size()) {
			st.str_push(out, unit);
			continue;
		}
		const char escaped = raw[++at];
		switch (escaped) {
			case 'n': st.str_push(out, '\n'); break;
			case 't': st.str_push(out, '\t'); break;
			case 'r': st.str_push(out, '\r'); break;
			case 'a': st.str_push(out, '\a'); break;
			case 'b': st.str_push(out, '\b'); break;
			case 'f': st.str_push(out, '\f'); break;
			case 'v': st.str_push(out, '\v'); break;
			case '0': st.str_push(out, '\0'); break;
			case '\\':
			case '\'':
			case '"': st.str_push(out, escaped); break;
			case '\n': break; // backslash-newline inside a literal joins lines
			default: // unknown escapes stay verbatim, as CPython keeps them
				st.str_push(out, '\\');
				st.str_push(out, escaped);
				break;
		}
	}
	return out;
}

// --- operator tag -> value-level enum ---------------------------------------

enum class bop : unsigned char { bor, bxor, band, shl, shr, add, sub, mul, div, floordiv, mod, pow_ };
enum class cop : unsigned char { lt, le, gt, ge, eq, ne, in, not_in, is, is_not };
enum class uop : unsigned char { not_, neg, pos, invert };

template <typename Op> struct bop_of { };
template <> struct bop_of<ast::op_bor> { static constexpr bop value = bop::bor; };
template <> struct bop_of<ast::op_bxor> { static constexpr bop value = bop::bxor; };
template <> struct bop_of<ast::op_band> { static constexpr bop value = bop::band; };
template <> struct bop_of<ast::op_shl> { static constexpr bop value = bop::shl; };
template <> struct bop_of<ast::op_shr> { static constexpr bop value = bop::shr; };
template <> struct bop_of<ast::op_add> { static constexpr bop value = bop::add; };
template <> struct bop_of<ast::op_sub> { static constexpr bop value = bop::sub; };
template <> struct bop_of<ast::op_mul> { static constexpr bop value = bop::mul; };
template <> struct bop_of<ast::op_div> { static constexpr bop value = bop::div; };
template <> struct bop_of<ast::op_floordiv> { static constexpr bop value = bop::floordiv; };
template <> struct bop_of<ast::op_mod> { static constexpr bop value = bop::mod; };
template <> struct bop_of<ast::op_pow> { static constexpr bop value = bop::pow_; };

template <typename Op> struct cop_of { };
template <> struct cop_of<ast::op_lt> { static constexpr cop value = cop::lt; };
template <> struct cop_of<ast::op_le> { static constexpr cop value = cop::le; };
template <> struct cop_of<ast::op_gt> { static constexpr cop value = cop::gt; };
template <> struct cop_of<ast::op_ge> { static constexpr cop value = cop::ge; };
template <> struct cop_of<ast::op_eq> { static constexpr cop value = cop::eq; };
template <> struct cop_of<ast::op_ne> { static constexpr cop value = cop::ne; };
template <> struct cop_of<ast::op_in> { static constexpr cop value = cop::in; };
template <> struct cop_of<ast::op_not_in> { static constexpr cop value = cop::not_in; };
template <> struct cop_of<ast::op_is> { static constexpr cop value = cop::is; };
template <> struct cop_of<ast::op_is_not> { static constexpr cop value = cop::is_not; };

template <typename Op> struct uop_of { };
template <> struct uop_of<ast::op_not> { static constexpr uop value = uop::not_; };
template <> struct uop_of<ast::op_neg> { static constexpr uop value = uop::neg; };
template <> struct uop_of<ast::op_pos> { static constexpr uop value = uop::pos; };
template <> struct uop_of<ast::op_invert> { static constexpr uop value = uop::invert; };

constexpr std::string_view bop_symbol(bop op) noexcept {
	switch (op) {
		case bop::bor: return "|";
		case bop::bxor: return "^";
		case bop::band: return "&";
		case bop::shl: return "<<";
		case bop::shr: return ">>";
		case bop::add: return "+";
		case bop::sub: return "-";
		case bop::mul: return "*";
		case bop::div: return "/";
		case bop::floordiv: return "//";
		case bop::mod: return "%";
		case bop::pow_: return "** or pow()";
	}
	return "?";
}
constexpr std::string_view cop_symbol(cop op) noexcept {
	switch (op) {
		case cop::lt: return "<";
		case cop::le: return "<=";
		case cop::gt: return ">";
		case cop::ge: return ">=";
		case cop::eq: return "==";
		case cop::ne: return "!=";
		case cop::in: return "in";
		case cop::not_in: return "not in";
		case cop::is: return "is";
		case cop::is_not: return "is not";
	}
	return "?";
}
constexpr std::string_view uop_symbol(uop op) noexcept {
	switch (op) {
		case uop::not_: return "not";
		case uop::neg: return "-";
		case uop::pos: return "+";
		case uop::invert: return "~";
	}
	return "?";
}

// --- kind predicates ---------------------------------------------------------

constexpr bool is_num(Kind kind) noexcept {
	return kind == Kind::boolean || kind == Kind::int_ || kind == Kind::float_;
}
constexpr bool is_int_like(Kind kind) noexcept {
	return kind == Kind::boolean || kind == Kind::int_;
}
constexpr double to_double(const Object & object) noexcept {
	return object.kind == Kind::float_ ? object.f : static_cast<double>(object.i);
}

// --- the generic value-level operations ----------------------------------------

// Python == : deep structural equality; the numeric kinds compare by
// value across each other (True == 1 == 1.0); unrelated kinds are just
// unequal, never an error
template <typename St> constexpr bool object_eq(const St & st, std::uint32_t li, std::uint32_t ri) noexcept {
	const Object & lhs = st.a.objs[li];
	const Object & rhs = st.a.objs[ri];
	if (is_num(lhs.kind) && is_num(rhs.kind)) {
		if (lhs.kind == Kind::float_ || rhs.kind == Kind::float_) {
			return to_double(lhs) == to_double(rhs);
		}
		return lhs.i == rhs.i;
	}
	if (lhs.kind != rhs.kind) {
		return false;
	}
	switch (lhs.kind) {
		case Kind::none: return true;
		case Kind::str: return st.str_of(lhs) == st.str_of(rhs);
		case Kind::tuple:
		case Kind::list: {
			if (lhs.count != rhs.count) {
				return false;
			}
			for (std::uint32_t at = 0; at < lhs.count; ++at) {
				if (!object_eq(st, lhs.first + at, rhs.first + at)) {
					return false;
				}
			}
			return true;
		}
		case Kind::range: {
			// CPython compares ranges as the sequences they denote
			const long long lhs_start = st.a.objs[lhs.first].i;
			const long long rhs_start = st.a.objs[rhs.first].i;
			const long long lhs_step = st.a.objs[lhs.first + 2].i;
			const long long rhs_step = st.a.objs[rhs.first + 2].i;
			const long long length = range_len(lhs_start, st.a.objs[lhs.first + 1].i, lhs_step);
			if (length != range_len(rhs_start, st.a.objs[rhs.first + 1].i, rhs_step)) {
				return false;
			}
			return length == 0 || (lhs_start == rhs_start && (length == 1 || lhs_step == rhs_step));
		}
		default: return false; // set/dict equality lands with M6
	}
}

// Python `is`: identity. Only the interned singletons (None, the two
// bools) are identical across separate evaluations; everything else is
// identical only to its own pool slot.
template <typename St> constexpr bool identical(const St & st, std::uint32_t li, std::uint32_t ri) noexcept {
	if (li == ri) {
		return true;
	}
	const Object & lhs = st.a.objs[li];
	const Object & rhs = st.a.objs[ri];
	if (lhs.kind != rhs.kind) {
		return false;
	}
	return lhs.kind == Kind::none || (lhs.kind == Kind::boolean && lhs.i == rhs.i);
}

// Python `in`: substring for str, element for the container kinds
// (containers themselves land in M6, the ranges are already walkable)
template <typename St> constexpr bool contains(St & st, std::uint32_t li, std::uint32_t ri) {
	const Object & haystack = st.a.objs[ri];
	switch (haystack.kind) {
		case Kind::str: {
			if (st.a.objs[li].kind != Kind::str) {
				st.raise_error(ex_kind::TypeError,
					{"'in <string>' requires string as left operand, not '", type_name(st.a.objs[li].kind), "'"});
				return false;
			}
			return st.str_of(ri).find(st.str_of(li)) != std::string_view::npos;
		}
		case Kind::tuple:
		case Kind::list:
		case Kind::set: {
			for (std::uint32_t at = 0; at < haystack.count; ++at) {
				if (object_eq(st, li, haystack.first + at)) {
					return true;
				}
			}
			return false;
		}
		case Kind::dict: {
			for (std::uint32_t at = 0; at < haystack.count; ++at) {
				if (object_eq(st, li, st.a.pairs[haystack.first + at].key)) {
					return true;
				}
			}
			return false;
		}
		case Kind::range: {
			// arithmetic membership - no iteration needed
			const Object & needle = st.a.objs[li];
			if (!is_num(needle.kind)) {
				return false;
			}
			long long value = 0;
			if (needle.kind == Kind::float_) {
				if (floor_d(needle.f) != needle.f) {
					return false;
				}
				value = static_cast<long long>(needle.f);
			} else {
				value = needle.i;
			}
			const long long start = st.a.objs[haystack.first].i;
			const long long stop = st.a.objs[haystack.first + 1].i;
			const long long step = st.a.objs[haystack.first + 2].i;
			if (step > 0) {
				return value >= start && value < stop && (value - start) % step == 0;
			}
			return value <= start && value > stop && (start - value) % (-step) == 0;
		}
		default:
			st.raise_error(ex_kind::TypeError,
				{"argument of type '", type_name(haystack.kind), "' is not iterable"});
			return false;
	}
}

template <typename St>
constexpr std::uint32_t binop_type_error(St & st, std::string_view symbol, Kind lhs, Kind rhs) noexcept {
	return st.raise_error(ex_kind::TypeError,
		{"unsupported operand type(s) for ", symbol, ": '", type_name(lhs), "' and '", type_name(rhs), "'"});
}

template <typename St> constexpr std::uint32_t str_repeat(St & st, const Object & value, long long times) {
	const std::uint32_t out = st.make_str_here();
	for (long long done = 0; done < times; ++done) {
		st.str_append(out, st.str_of(value));
	}
	return out;
}

template <typename St> constexpr std::uint32_t int_pow(St & st, long long base, long long exp) {
	long long result = 1;
	while (exp > 0) {
		if ((exp & 1) != 0) {
			if (mul_overflows(result, base)) {
				return st.raise_error(ex_kind::OverflowError, "ctpy: ** result does not fit the 64-bit v0.1 int");
			}
			result *= base;
		}
		exp >>= 1;
		if (exp > 0) {
			if (mul_overflows(base, base)) {
				return st.raise_error(ex_kind::OverflowError, "ctpy: ** result does not fit the 64-bit v0.1 int");
			}
			base *= base;
		}
	}
	return st.make_int(result);
}

// the arithmetic / bitwise binary operators (or/and never reach here -
// they short-circuit in the evaluator before both sides exist)
template <typename St> constexpr std::uint32_t bin_op(St & st, bop op, std::uint32_t li, std::uint32_t ri) {
	const Object lhs = st.a.objs[li]; // copies: the pool below may grow
	const Object rhs = st.a.objs[ri];
	const bool both_int = is_int_like(lhs.kind) && is_int_like(rhs.kind);
	const bool both_num = is_num(lhs.kind) && is_num(rhs.kind);
	switch (op) {
		case bop::add:
			if (lhs.kind == Kind::str && rhs.kind == Kind::str) {
				const std::uint32_t out = st.make_str_here();
				st.str_append(out, st.str_of(lhs));
				st.str_append(out, st.str_of(rhs));
				return out;
			}
			if (both_num) {
				if (both_int) {
					return st.make_int(lhs.i + rhs.i);
				}
				return st.make_float(to_double(lhs) + to_double(rhs));
			}
			break;
		case bop::sub:
			if (both_num) {
				if (both_int) {
					return st.make_int(lhs.i - rhs.i);
				}
				return st.make_float(to_double(lhs) - to_double(rhs));
			}
			break;
		case bop::mul:
			if (lhs.kind == Kind::str && is_int_like(rhs.kind)) {
				return str_repeat(st, lhs, rhs.i);
			}
			if (rhs.kind == Kind::str && is_int_like(lhs.kind)) {
				return str_repeat(st, rhs, lhs.i);
			}
			if (both_num) {
				if (both_int) {
					return st.make_int(lhs.i * rhs.i);
				}
				return st.make_float(to_double(lhs) * to_double(rhs));
			}
			break;
		case bop::div:
			if (both_num) {
				const double denominator = to_double(rhs);
				if (denominator == 0.0) {
					return st.raise_error(ex_kind::ZeroDivisionError,
						both_int ? "division by zero" : "float division by zero");
				}
				return st.make_float(to_double(lhs) / denominator);
			}
			break;
		case bop::floordiv:
			if (both_int) {
				if (rhs.i == 0) {
					return st.raise_error(ex_kind::ZeroDivisionError, "integer division or modulo by zero");
				}
				return st.make_int(floordiv_ll(lhs.i, rhs.i));
			}
			if (both_num) {
				const double denominator = to_double(rhs);
				if (denominator == 0.0) {
					return st.raise_error(ex_kind::ZeroDivisionError, "float floor division by zero");
				}
				return st.make_float(floor_d(to_double(lhs) / denominator));
			}
			break;
		case bop::mod:
			if (both_int) {
				if (rhs.i == 0) {
					return st.raise_error(ex_kind::ZeroDivisionError, "integer division or modulo by zero");
				}
				return st.make_int(mod_ll(lhs.i, rhs.i));
			}
			if (both_num) {
				const double denominator = to_double(rhs);
				if (denominator == 0.0) {
					return st.raise_error(ex_kind::ZeroDivisionError, "float modulo");
				}
				return st.make_float(mod_d(to_double(lhs), denominator));
			}
			break;
		case bop::pow_:
			if (both_num) {
				long long exp = 0;
				if (rhs.kind == Kind::float_) {
					if (floor_d(rhs.f) != rhs.f) {
						return st.raise_error(ex_kind::TypeError,
							"ctpy v0.1: ** supports only an integral exponent (no constexpr pow in C++20)");
					}
					exp = static_cast<long long>(rhs.f);
				} else {
					exp = rhs.i;
				}
				if (is_int_like(lhs.kind) && rhs.kind != Kind::float_) {
					if (exp >= 0) {
						return int_pow(st, lhs.i, exp);
					}
					if (lhs.i == 0) {
						return st.raise_error(ex_kind::ZeroDivisionError,
							"0.0 cannot be raised to a negative power");
					}
					return st.make_float(ipow_d(static_cast<double>(lhs.i), exp));
				}
				const double base = to_double(lhs);
				if (base == 0.0 && exp < 0) {
					return st.raise_error(ex_kind::ZeroDivisionError,
						"0.0 cannot be raised to a negative power");
				}
				return st.make_float(ipow_d(base, exp));
			}
			break;
		case bop::bor:
			if (both_int) {
				return st.make_int(lhs.i | rhs.i);
			}
			break;
		case bop::bxor:
			if (both_int) {
				return st.make_int(lhs.i ^ rhs.i);
			}
			break;
		case bop::band:
			if (both_int) {
				return st.make_int(lhs.i & rhs.i);
			}
			break;
		case bop::shl:
			if (both_int) {
				if (rhs.i < 0) {
					return st.raise_error(ex_kind::ValueError, "negative shift count");
				}
				if (rhs.i >= 63) {
					if (lhs.i == 0) {
						return st.make_int(0);
					}
					return st.raise_error(ex_kind::OverflowError,
						"ctpy: << result does not fit the 64-bit v0.1 int");
				}
				return st.make_int(lhs.i << rhs.i);
			}
			break;
		case bop::shr:
			if (both_int) {
				if (rhs.i < 0) {
					return st.raise_error(ex_kind::ValueError, "negative shift count");
				}
				if (rhs.i >= 63) {
					return st.make_int(lhs.i < 0 ? -1 : 0);
				}
				return st.make_int(lhs.i >> rhs.i);
			}
			break;
	}
	return binop_type_error(st, bop_symbol(op), lhs.kind, rhs.kind);
}

// one comparison link; ordering across unrelated kinds raises
template <typename St> constexpr bool compare_op(St & st, cop op, std::uint32_t li, std::uint32_t ri) {
	switch (op) {
		case cop::eq: return object_eq(st, li, ri);
		case cop::ne: return !object_eq(st, li, ri);
		case cop::is: return identical(st, li, ri);
		case cop::is_not: return !identical(st, li, ri);
		case cop::in: return contains(st, li, ri);
		case cop::not_in: return !contains(st, li, ri);
		default: break;
	}
	const Object & lhs = st.a.objs[li];
	const Object & rhs = st.a.objs[ri];
	int order = 0;
	if (is_num(lhs.kind) && is_num(rhs.kind)) {
		if (lhs.kind == Kind::float_ || rhs.kind == Kind::float_) {
			const double a = to_double(lhs);
			const double b = to_double(rhs);
			order = a < b ? -1 : (b < a ? 1 : 0);
		} else {
			order = lhs.i < rhs.i ? -1 : (rhs.i < lhs.i ? 1 : 0);
		}
	} else if (lhs.kind == Kind::str && rhs.kind == Kind::str) {
		const int raw = st.str_of(lhs).compare(st.str_of(rhs));
		order = raw < 0 ? -1 : (raw > 0 ? 1 : 0);
	} else {
		st.raise_error(ex_kind::TypeError,
			{"'", cop_symbol(op), "' not supported between instances of '",
			 type_name(lhs.kind), "' and '", type_name(rhs.kind), "'"});
		return false;
	}
	switch (op) {
		case cop::lt: return order < 0;
		case cop::le: return order <= 0;
		case cop::gt: return order > 0;
		case cop::ge: return order >= 0;
		default: return false;
	}
}

template <typename St> constexpr std::uint32_t unary_op(St & st, uop op, std::uint32_t vi) {
	const Object value = st.a.objs[vi];
	switch (op) {
		case uop::not_:
			return st.make_bool(!st.truthy(vi));
		case uop::neg:
			if (value.kind == Kind::float_) {
				return st.make_float(-value.f);
			}
			if (is_int_like(value.kind)) {
				return st.make_int(-value.i);
			}
			break;
		case uop::pos:
			if (value.kind == Kind::float_) {
				return st.make_float(value.f);
			}
			if (is_int_like(value.kind)) {
				return st.make_int(value.i);
			}
			break;
		case uop::invert:
			if (is_int_like(value.kind)) {
				return st.make_int(~value.i);
			}
			break;
	}
	return st.raise_error(ex_kind::TypeError,
		{"bad operand type for unary ", uop_symbol(op), ": '", type_name(value.kind), "'"});
}

// --- the tree-walk -------------------------------------------------------------

template <typename Node, typename St> constexpr std::uint32_t eval_node(St & st);

// no specialization = the node kind is not evaluable yet: set/dict
// displays and subscripts land in M6, user-function calls in M5,
// f-strings in M7 (builtin name calls are specialized in builtins.hpp)
template <typename Node> struct evaluator {
	static_assert(sizeof(Node) == 0, "ctpy: this AST node kind is not evaluable yet (later milestone)");
};

template <typename Text> struct evaluator<ast::int_lit<Text>> {
	template <typename St> static constexpr std::uint32_t run(St & st) {
		return st.make_int(parse_int(Text::view()));
	}
};
template <typename Text> struct evaluator<ast::float_lit<Text>> {
	template <typename St> static constexpr std::uint32_t run(St & st) {
		return st.make_float(parse_float(Text::view()));
	}
};
template <typename Text> struct evaluator<ast::str_lit<Text>> {
	template <typename St> static constexpr std::uint32_t run(St & st) {
		return make_cooked_str(st, Text::view());
	}
};
template <> struct evaluator<ast::none_lit> {
	template <typename St> static constexpr std::uint32_t run(St & st) {
		return st.none();
	}
};
template <> struct evaluator<ast::true_lit> {
	template <typename St> static constexpr std::uint32_t run(St & st) {
		return st.make_bool(true);
	}
};
template <> struct evaluator<ast::false_lit> {
	template <typename St> static constexpr std::uint32_t run(St & st) {
		return st.make_bool(false);
	}
};

template <typename Text> struct evaluator<ast::name<Text>> {
	template <typename St> static constexpr std::uint32_t run(St & st) {
		const std::uint32_t found = st.lookup(Text::view());
		if (found == not_found) {
			return st.raise_error(ex_kind::NameError, {"name '", Text::view(), "' is not defined"});
		}
		return found;
	}
};

// evaluate a display's elements (results scatter through the pool as
// subexpressions allocate), then copy the element OBJECTS into one
// contiguous run so the container can be an index-range. Copies share
// their char/child ranges - the pools are append-only, nothing moves.
template <Kind ContainerKind, typename... Es, typename St>
constexpr std::uint32_t make_sequence(St & st) {
	std::uint32_t items[sizeof...(Es) + 1]{};
	std::size_t at = 0;
	const bool complete = ((items[at++] = eval_node<Es, St>(st), !st.raised) && ...);
	(void)complete;
	(void)at;
	if (st.raised) {
		return st.none();
	}
	const std::uint32_t first = static_cast<std::uint32_t>(st.a.objs.size());
	for (std::size_t element = 0; element < sizeof...(Es); ++element) {
		st.a.objs.push_back(st.a.objs[items[element]]);
	}
	return st.push(Object{.kind = ContainerKind,
	                      .first = first,
	                      .count = static_cast<std::uint32_t>(sizeof...(Es))});
}

// tuple/list displays are already needed by M4 (tuple unpacking and
// for-iteration); the other displays and all indexing land in M6
template <typename... Es> struct evaluator<ast::tuple_expr<Es...>> {
	template <typename St> static constexpr std::uint32_t run(St & st) {
		return make_sequence<Kind::tuple, Es...>(st);
	}
};
template <typename... Es> struct evaluator<ast::list_expr<Es...>> {
	template <typename St> static constexpr std::uint32_t run(St & st) {
		return make_sequence<Kind::list, Es...>(st);
	}
};

template <typename Op, typename E> struct evaluator<ast::unary_expr<Op, E>> {
	template <typename St> static constexpr std::uint32_t run(St & st) {
		const std::uint32_t value = eval_node<E, St>(st);
		if (st.raised) {
			return st.none();
		}
		return unary_op(st, uop_of<Op>::value, value);
	}
};

template <typename Op, typename L, typename R> struct evaluator<ast::binary_expr<Op, L, R>> {
	template <typename St> static constexpr std::uint32_t run(St & st) {
		if constexpr (std::is_same_v<Op, ast::op_or>) {
			// short-circuit; the result is the deciding OPERAND
			const std::uint32_t left = eval_node<L, St>(st);
			if (st.raised) {
				return st.none();
			}
			return st.truthy(left) ? left : eval_node<R, St>(st);
		} else if constexpr (std::is_same_v<Op, ast::op_and>) {
			const std::uint32_t left = eval_node<L, St>(st);
			if (st.raised) {
				return st.none();
			}
			return st.truthy(left) ? eval_node<R, St>(st) : left;
		} else {
			const std::uint32_t left = eval_node<L, St>(st);
			if (st.raised) {
				return st.none();
			}
			const std::uint32_t right = eval_node<R, St>(st);
			if (st.raised) {
				return st.none();
			}
			return bin_op(st, bop_of<Op>::value, left, right);
		}
	}
};

// a < b < c evaluates b once and short-circuits on the first false link
template <typename... Links> struct cmp_chain;
template <> struct cmp_chain<> {
	template <typename St> static constexpr std::uint32_t run(St &, std::uint32_t) noexcept {
		return St::true_index;
	}
};
template <typename Op, typename R, typename... Rest> struct cmp_chain<ast::cmp_link<Op, R>, Rest...> {
	template <typename St> static constexpr std::uint32_t run(St & st, std::uint32_t left) {
		const std::uint32_t right = eval_node<R, St>(st);
		if (st.raised) {
			return st.none();
		}
		const bool held = compare_op(st, cop_of<Op>::value, left, right);
		if (st.raised) {
			return st.none();
		}
		if (!held) {
			return St::false_index;
		}
		return cmp_chain<Rest...>::run(st, right);
	}
};

template <typename L, typename... Links> struct evaluator<ast::compare_expr<L, Links...>> {
	template <typename St> static constexpr std::uint32_t run(St & st) {
		const std::uint32_t left = eval_node<L, St>(st);
		if (st.raised) {
			return st.none();
		}
		return cmp_chain<Links...>::run(st, left);
	}
};

template <typename Cond, typename Then, typename Else>
struct evaluator<ast::ternary_expr<Cond, Then, Else>> {
	template <typename St> static constexpr std::uint32_t run(St & st) {
		const std::uint32_t condition = eval_node<Cond, St>(st);
		if (st.raised) {
			return st.none();
		}
		// only the taken branch is evaluated
		return st.truthy(condition) ? eval_node<Then, St>(st) : eval_node<Else, St>(st);
	}
};

template <typename Node, typename St> constexpr std::uint32_t eval_node(St & st) {
	if (st.raised) {
		return st.none(); // exception in flight: every step short-circuits
	}
	return evaluator<Node>::run(st);
}

// unwrap ctpy::eval<Src>'s module: it must hold exactly one expression
template <typename M> struct single_expr {
	static_assert(sizeof(M) == 0,
		"ctpy::eval<Src> evaluates a single EXPRESSION; statements need ctpy::run<Src> (M8)");
};
template <typename E> struct single_expr<ast::module<ast::expr_stmt<E>>> {
	using type = E;
};

} // namespace detail

// evaluate one AST node inside a live interpreter state; returns the
// object-pool index of the result (None if an exception is in flight)
CTPY_EXPORT template <typename Node, typename St> constexpr std::uint32_t eval(St & st) {
	return detail::eval_node<Node, St>(st);
}

// The result of ctpy::eval<Src>: one scalar-ish Python value copied
// out of the (dead) arena, or the Python exception that ended the
// evaluation. INTERIM shape - M8's run<>/value views replace the
// payload side wholesale; only ok()/exception()/to<T>()/str() are
// meant to be leaned on.
CTPY_EXPORT struct eval_result {
	static constexpr std::size_t str_capacity = 256;

	Kind kind = Kind::none;
	long long int_value = 0;
	double float_value = 0.0;
	ctc::string<str_capacity> str_value{};
	bool raised = false;
	PyError error{};

	constexpr bool ok() const noexcept {
		return !raised;
	}
	constexpr const PyError & exception() const noexcept {
		return error;
	}
	constexpr std::string_view str() const noexcept {
		return str_value.view();
	}
	template <typename T> constexpr T to() const noexcept {
		if constexpr (std::is_same_v<T, bool>) {
			switch (kind) {
				case Kind::boolean:
				case Kind::int_: return int_value != 0;
				case Kind::float_: return float_value != 0.0;
				case Kind::str: return !str_value.empty();
				default: return false;
			}
		} else if constexpr (std::is_floating_point_v<T>) {
			return kind == Kind::float_ ? static_cast<T>(float_value) : static_cast<T>(int_value);
		} else {
			static_assert(std::is_integral_v<T>, "eval_result::to<T>: T must be arithmetic in M3");
			return kind == Kind::float_ ? static_cast<T>(float_value) : static_cast<T>(int_value);
		}
	}
};

// sugar: evaluate one Python expression at compile time.
//   static_assert(ctpy::eval<"2 ** 10">().to<int>() == 1024);
// Family policy: a NON-PARSING source hard-errors here (static_assert
// names the stage); a RAISING expression is a soft ok()==false result.
CTPY_EXPORT template <ctll::fixed_string Src, typename ArenaT = Arena<>>
constexpr eval_result eval() noexcept {
	static_assert(is_valid<Src>, "ctpy::eval<Src>: the source failed to pre-lex or parse");
	using Expr = typename detail::single_expr<detail::parsed_module<Src>>::type;
	State<ArenaT> st{};
	const std::uint32_t value = detail::eval_node<Expr>(st);
	eval_result out{};
	out.raised = st.raised;
	out.error = st.error;
	if (!st.raised) {
		const Object & object = st.a.objs[value];
		out.kind = object.kind;
		switch (object.kind) {
			case Kind::boolean:
			case Kind::int_:
				out.int_value = object.i;
				break;
			case Kind::float_:
				out.float_value = object.f;
				break;
			case Kind::str: {
				const std::string_view content = st.str_of(object);
				out.str_value.append(content.size() <= eval_result::str_capacity
					? content
					: content.substr(0, eval_result::str_capacity));
				break;
			}
			default:
				break; // container payloads land with the M8 value views
		}
	}
	return out;
}

} // namespace ctpy

#endif
