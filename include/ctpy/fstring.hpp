#ifndef CTPY__FSTRING__HPP
#define CTPY__FSTRING__HPP

#include "version.hpp"
#include "text.hpp"
#include "ast.hpp"
#include "object.hpp"
#include "parse.hpp"
#include "eval.hpp"
#include "builtins.hpp"

#ifndef CTPY_IN_A_MODULE
#include <cstddef>
#include <cstdint>
#include <string_view>
#endif

// f-strings. The grammar keeps an f-string's body VERBATIM in one
// ast::fstr_lit<Text> node (M2 deferred in-grammar hole parsing), so the
// {expr} holes are parsed here - at the SAME grammar level as any other
// expression: each hole's text is re-run through the full Tablewright
// table via ctpy::detail::parsed_module, which is exactly what a
// parenthesized expression elsewhere in the script goes through. The
// scan into literal/hole parts happens once per fstr_lit TYPE
// (consteval); evaluation then walks the parts per execution.
//
// Supported: {expr} holes (any v0.1 expression, including nested
// f-strings in the other quote kind), {{ and }} escapes, backslash
// escapes in the literal segments. NOT supported (v0.1, all hard
// compile errors with named messages): format specs ({x:>8}),
// conversions ({x!r}), the = debug specifier, empty holes. A malformed
// body (lone '}', unterminated '{') is a hard error too - the module
// grammar accepted the f-string as one token, so by family policy the
// failure names the stage. Hole values format with str(), not repr(),
// like CPython's default conversion.

namespace ctpy {

namespace detail {

// --- the once-per-type body scan -------------------------------------------

enum class fstring_issue : unsigned char {
	none,
	lone_close,  // "}" outside a hole (write "}}")
	unterminated, // "{" whose hole never closes
	empty_hole,  // "{}" or "{ }"
	format_spec, // "{x:...}" - not in v0.1
	conversion,  // "{x!r}" - not in v0.1
};

struct fstring_part {
	std::size_t from = 0;
	std::size_t to = 0;
	bool is_hole = false;
};

template <std::size_t MaxParts> struct fstring_scan_result {
	fstring_part parts[MaxParts ? MaxParts : 1]{};
	std::size_t count = 0;
	fstring_issue issue = fstring_issue::none;
};

// split a raw body into literal segments and {expr} holes. Hole
// boundaries respect nested brackets and string quotes ('}' inside
// "{d['}']}" does not close the hole); a ':' or a '!' (that is not
// '!=') at hole top level is a format spec/conversion - out of v0.1.
// Hole text is trimmed of blanks ("{ x }" is CPython-legal).
template <typename Text> consteval auto fstring_scan() noexcept {
	constexpr std::string_view body = Text::view();
	fstring_scan_result<Text::size() + 1> out{};
	std::size_t at = 0;
	std::size_t literal_from = 0;
	while (at < body.size()) {
		const char unit = body[at];
		if (unit == '{') {
			if (at + 1 < body.size() && body[at + 1] == '{') {
				at += 2; // literal "{{", cooked later
				continue;
			}
			if (literal_from < at) {
				out.parts[out.count] = fstring_part{literal_from, at, false};
				++out.count;
			}
			std::size_t scan = at + 1;
			int nest = 0;
			char quote = '\0';
			bool closed = false;
			for (; scan < body.size(); ++scan) {
				const char inner = body[scan];
				if (quote != '\0') {
					if (inner == '\\') {
						++scan;
					} else if (inner == quote) {
						quote = '\0';
					}
					continue;
				}
				if (inner == '\'' || inner == '"') {
					quote = inner;
					continue;
				}
				if (inner == '(' || inner == '[' || inner == '{') {
					++nest;
					continue;
				}
				if (inner == ')' || inner == ']') {
					if (nest > 0) {
						--nest;
					}
					continue;
				}
				if (inner == '}') {
					if (nest == 0) {
						closed = true;
						break;
					}
					--nest;
					continue;
				}
				if (nest == 0 && inner == ':') {
					out.issue = fstring_issue::format_spec;
					return out;
				}
				if (nest == 0 && inner == '!' &&
				    (scan + 1 >= body.size() || body[scan + 1] != '=')) {
					out.issue = fstring_issue::conversion;
					return out;
				}
			}
			if (!closed) {
				out.issue = fstring_issue::unterminated;
				return out;
			}
			std::size_t from = at + 1;
			std::size_t to = scan;
			while (from < to && (body[from] == ' ' || body[from] == '\t')) {
				++from;
			}
			while (to > from && (body[to - 1] == ' ' || body[to - 1] == '\t')) {
				--to;
			}
			if (from == to) {
				out.issue = fstring_issue::empty_hole;
				return out;
			}
			out.parts[out.count] = fstring_part{from, to, true};
			++out.count;
			at = scan + 1;
			literal_from = at;
			continue;
		}
		if (unit == '}') {
			if (at + 1 < body.size() && body[at + 1] == '}') {
				at += 2; // literal "}}", cooked later
				continue;
			}
			out.issue = fstring_issue::lone_close;
			return out;
		}
		++at;
	}
	if (literal_from < body.size()) {
		out.parts[out.count] = fstring_part{literal_from, body.size(), false};
		++out.count;
	}
	return out;
}

template <typename Text> inline constexpr auto fstring_scan_of = fstring_scan<Text>();

// one hole's text as a fresh parser subject (the prelex/parse pipeline
// takes a ctll::fixed_string NTTP, same as a whole script)
template <typename Text, std::size_t From, std::size_t Len>
consteval auto fstring_hole_source() noexcept {
	ctll::fixed_string<Len> out{};
	for (std::size_t at = 0; at < Len; ++at) {
		out.content[at] = static_cast<char32_t>(static_cast<unsigned char>(Text::storage[From + at]));
	}
	out.real_size = Len;
	return out;
}

// a hole must parse to a module holding exactly one expression
template <typename M> struct fstring_hole_expr {
	static_assert(sizeof(M) == 0,
		"ctpy f-string: a {...} hole must hold a single expression (statements are not allowed)");
};
template <unsigned N, typename E>
struct fstring_hole_expr<ast::module<ast::lined<N, ast::expr_stmt<E>>>> {
	using type = E;
};

// --- evaluation --------------------------------------------------------------

// cook one literal segment into the in-flight str: backslash escapes
// (shared with plain str literals) plus the {{ }} doublings
template <typename St>
constexpr void fstring_cook_segment(St & st, std::uint32_t out, std::string_view raw) {
	for (std::size_t at = 0; at < raw.size(); ++at) {
		const char unit = raw[at];
		if ((unit == '{' || unit == '}') && at + 1 < raw.size() && raw[at + 1] == unit) {
			st.str_push(out, unit);
			++at;
			continue;
		}
		if (unit != '\\' || at + 1 == raw.size()) {
			st.str_push(out, unit);
			continue;
		}
		push_cooked_escape(st, out, raw[++at]);
	}
}

// phase 1: evaluate every hole, in source order, BEFORE the result str
// exists - hole evaluation allocates freely (its own strs, containers),
// which must not interleave with the result's char run
template <typename Text, std::size_t At, std::size_t N> struct fstring_eval_holes {
	template <typename St>
	static constexpr void run(St & st, std::uint32_t * holes, std::size_t & used) {
		if constexpr (At < N) {
			constexpr auto part = fstring_scan_of<Text>.parts[At];
			if constexpr (part.is_hole) {
				constexpr auto source = fstring_hole_source<Text, part.from, part.to - part.from>();
				static_assert(is_valid<source>,
					"ctpy f-string: the expression inside a {...} hole failed to parse");
				using Expr = typename fstring_hole_expr<parsed_module<source>>::type;
				holes[used] = eval_node<Expr, St>(st);
				++used;
				if (st.raised) {
					return;
				}
			}
			fstring_eval_holes<Text, At + 1, N>::run(st, holes, used);
		}
	}
};

// phase 2: append every part to the result str - literal segments cook,
// hole values format with str() semantics; nothing here allocates
// objects, so the result's char run stays contiguous
template <typename Text, std::size_t At, std::size_t N> struct fstring_write_parts {
	template <typename St>
	static constexpr void run(St & st, std::uint32_t out, const std::uint32_t * holes,
	                          std::size_t & used) {
		if constexpr (At < N) {
			constexpr auto part = fstring_scan_of<Text>.parts[At];
			if constexpr (part.is_hole) {
				str_sink<St> sink{st, out};
				write_object(st, sink, holes[used], false);
				++used;
			} else {
				fstring_cook_segment(st, out, Text::view().substr(part.from, part.to - part.from));
			}
			fstring_write_parts<Text, At + 1, N>::run(st, out, holes, used);
		}
	}
};

template <typename Text> struct evaluator<ast::fstr_lit<Text>> {
	template <typename St> static constexpr std::uint32_t run(St & st) {
		constexpr auto scan = fstring_scan_of<Text>;
		static_assert(scan.issue != fstring_issue::lone_close,
			"ctpy f-string: single '}' is not allowed (write '}}')");
		static_assert(scan.issue != fstring_issue::unterminated,
			"ctpy f-string: expected '}' before the end of the string");
		static_assert(scan.issue != fstring_issue::empty_hole,
			"ctpy f-string: empty expression not allowed inside {...}");
		static_assert(scan.issue != fstring_issue::format_spec,
			"ctpy v0.1 f-string: format specs ({x:...}) are not supported");
		static_assert(scan.issue != fstring_issue::conversion,
			"ctpy v0.1 f-string: conversions ({x!r}) are not supported");
		std::uint32_t holes[scan.count + 1]{};
		std::size_t used = 0;
		fstring_eval_holes<Text, 0, scan.count>::run(st, holes, used);
		if (st.raised) {
			return st.none();
		}
		const std::uint32_t out = st.make_str_here();
		used = 0;
		fstring_write_parts<Text, 0, scan.count>::run(st, out, holes, used);
		return out;
	}
};

} // namespace detail

} // namespace ctpy

#endif
