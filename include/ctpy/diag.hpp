#ifndef CTPY__DIAG__HPP
#define CTPY__DIAG__HPP

#include "version.hpp"
#include "prelex.hpp"
#include "parse.hpp"

#ifndef CTPY_IN_A_MODULE
#include <cstddef>
#include <string_view>
#endif

// Queryable diagnostics for scripts that fail to pre-lex or parse (the
// ctlark diag.hpp architecture, re-authored for ctpy's two-stage
// pipeline). ctpy::is_valid<Src> stays a plain bool; when it is false,
// ctpy::error_info<Src>() says WHICH STAGE rejected the script (prelex
// or parse), WHAT went wrong, and WHERE (byte offset, 1-based line and
// column in the RAW source - parse positions are mapped back through
// the pre-lexer's marker-to-source table), and error_message<Src>()
// renders the whole story into static storage as one caret diagnostic:
//
//   ctpy: parse error (SyntaxError) at line 2, column 9: invalid syntax
//     y = 1 +
//             ^
//
// Everything is computed at compile time via the family size-pass/
// fill-pass idiom; neither entry point ever hard-errors - the hard
// failure lives in the parse-demanding entry points (run/eval/module),
// whose static_asserts name the stage (parse.hpp: require_valid).

namespace ctpy {

// which pipeline stage rejected the script (none = it is valid)
CTPY_EXPORT enum class error_stage : unsigned char {
	none,   // the script pre-lexes and parses
	prelex, // indentation / string-termination rewriting failed
	parse   // the marker stream does not derive from the grammar
};

CTPY_EXPORT constexpr std::string_view to_string(error_stage stage) noexcept {
	switch (stage) {
		case error_stage::none: return "none";
		case error_stage::prelex: return "prelex";
		case error_stage::parse: return "parse";
	}
	return "unknown";
}

// what went wrong, unified across both stages
CTPY_EXPORT enum class error_kind : unsigned char {
	none,
	inconsistent_dedent, // prelex: a dedent to a depth never indented to
	unterminated_string, // prelex: a string literal never closed
	indent_too_deep,     // prelex: deeper than the indent stack
	syntax               // parse: the input does not match the grammar
};

CTPY_EXPORT constexpr std::string_view to_string(error_kind kind) noexcept {
	switch (kind) {
		case error_kind::none: return "none";
		case error_kind::inconsistent_dedent: return "inconsistent dedent";
		case error_kind::unterminated_string: return "unterminated string";
		case error_kind::indent_too_deep: return "indentation too deep";
		case error_kind::syntax: return "syntax error";
	}
	return "unknown";
}

// the exception CPython would raise for this kind (rendered into the
// message so the diagnostic reads like a traceback header)
CTPY_EXPORT constexpr std::string_view python_exception_name(error_kind kind) noexcept {
	switch (kind) {
		case error_kind::none: return "";
		case error_kind::inconsistent_dedent: return "IndentationError";
		case error_kind::unterminated_string: return "SyntaxError";
		case error_kind::indent_too_deep: return "IndentationError";
		case error_kind::syntax: return "SyntaxError";
	}
	return "";
}

// a byte offset resolved to 1-based line and column
CTPY_EXPORT struct source_position {
	std::size_t offset = 0;
	std::size_t line = 1;
	std::size_t column = 1;
};

CTPY_EXPORT constexpr source_position locate(std::string_view text, std::size_t offset) noexcept {
	source_position position{};
	if (offset > text.size()) {
		offset = text.size();
	}
	position.offset = offset;
	for (std::size_t at = 0; at < offset; ++at) {
		if (text[at] == '\n') {
			++position.line;
			position.column = 1;
		} else {
			++position.column;
		}
	}
	return position;
}

// everything a rejected script knows, as one value; position/line/
// column always refer to the RAW source text
CTPY_EXPORT struct error_info_t {
	error_stage stage = error_stage::none;
	error_kind kind = error_kind::none;
	std::size_t position = 0;
	std::size_t line = 1;
	std::size_t column = 1;

	constexpr bool ok() const noexcept {
		return stage == error_stage::none;
	}
};

namespace detail {

// the raw source as chars in static storage (fixed_strings hold
// char32_t units carrying bytes), for locate() and the caret snippet
template <ctll::fixed_string Src> struct source_text {
	static constexpr std::size_t length = Src.size();
	struct out_t {
		char content[length + 1]{};
	};
	static constexpr out_t make() noexcept {
		out_t out{};
		for (std::size_t at = 0; at < length; ++at) {
			out.content[at] = static_cast<char>(static_cast<unsigned char>(Src[at]));
		}
		return out;
	}
	static constexpr out_t content = make();
	static constexpr std::string_view view{content.content, length};
};

constexpr error_kind kind_of(prelex_error_kind kind) noexcept {
	switch (kind) {
		case prelex_error_kind::none: return error_kind::none;
		case prelex_error_kind::inconsistent_dedent: return error_kind::inconsistent_dedent;
		case prelex_error_kind::unterminated_string: return error_kind::unterminated_string;
		case prelex_error_kind::too_deep: return error_kind::indent_too_deep;
	}
	return error_kind::none;
}

// where in the RAW source a failed parse stopped: the CTLL result's
// position indexes the MARKER stream, mapped back through src_map
template <ctll::fixed_string Src> constexpr std::size_t parse_error_offset() noexcept {
	constexpr std::size_t marker_pos =
		parse_def<Src>::template output<start_context>::position;
	if (marker_pos < prelex_raw<Src>.src_map.size()) {
		return static_cast<std::size_t>(prelex_raw<Src>.src_map[marker_pos]);
	}
	return Src.size(); // rejected at end of input
}

template <ctll::fixed_string Src> consteval error_info_t compute_error_info() noexcept {
	error_info_t info{};
	if constexpr (!prelex_ok<Src>) {
		info.stage = error_stage::prelex;
		info.kind = kind_of(prelex_diag<Src>.kind);
		info.position = prelex_diag<Src>.offset;
	} else if constexpr (!is_valid<Src>) {
		info.stage = error_stage::parse;
		info.kind = error_kind::syntax;
		info.position = parse_error_offset<Src>();
	} else {
		return info; // valid: everything stays at its none/1/1 defaults
	}
	const source_position at = locate(source_text<Src>::view, info.position);
	info.line = at.line;
	info.column = at.column;
	return info;
}

template <ctll::fixed_string Src>
inline constexpr error_info_t error_info_of = compute_error_info<Src>();

// --- rendering (size pass with count_sink, fill pass with fill_sink) ---

struct count_sink {
	std::size_t at = 0;
	constexpr void put(std::string_view part) noexcept {
		at += part.size();
	}
};

struct fill_sink {
	char * out;
	std::size_t at = 0;
	constexpr void put(std::string_view part) noexcept {
		for (const char unit : part) {
			out[at++] = unit;
		}
	}
};

template <typename Sink> constexpr void put_uint(Sink & sink, std::size_t value) noexcept {
	char digits[20]{};
	std::size_t used = 0;
	do {
		digits[used++] = static_cast<char>('0' + static_cast<char>(value % 10));
		value /= 10;
	} while (value > 0);
	for (std::size_t at = 0; at < used / 2; ++at) {
		const char kept = digits[at];
		digits[at] = digits[used - 1 - at];
		digits[used - 1 - at] = kept;
	}
	sink.put(std::string_view{digits, used});
}

// the source line around the position, windowed so the caret always
// stays visible on pathologically long lines
inline constexpr std::size_t snippet_width = 72;
inline constexpr std::size_t snippet_caret_max = 60;

template <typename Sink>
constexpr void render_snippet(Sink & sink, std::string_view text, std::size_t pos) noexcept {
	if (pos > text.size()) {
		pos = text.size();
	}
	std::size_t line_start = pos;
	while (line_start > 0 && text[line_start - 1] != '\n') {
		--line_start;
	}
	std::size_t line_end = pos;
	while (line_end < text.size() && text[line_end] != '\n') {
		++line_end;
	}
	std::size_t window_start = line_start;
	if (pos - window_start > snippet_caret_max) {
		window_start = pos - snippet_caret_max;
	}
	std::size_t window_end = line_end;
	if (window_end - window_start > snippet_width) {
		window_end = window_start + snippet_width;
	}
	sink.put("\n  ");
	for (std::size_t at = window_start; at < window_end; ++at) {
		const char unit = text[at];
		sink.put((unit == '\t' || unit == '\r') ? std::string_view{" "} : text.substr(at, 1));
	}
	sink.put("\n  ");
	for (std::size_t at = window_start; at < pos; ++at) {
		sink.put(" ");
	}
	sink.put("^");
}

constexpr std::string_view kind_detail(error_kind kind, bool at_end) noexcept {
	switch (kind) {
		case error_kind::none:
			return "";
		case error_kind::inconsistent_dedent:
			return "unindent does not match any outer indentation level";
		case error_kind::unterminated_string:
			return "unterminated string literal";
		case error_kind::indent_too_deep:
			return "too many levels of indentation";
		case error_kind::syntax:
			return at_end ? "unexpected end of input" : "invalid syntax";
	}
	return "";
}

// "ctpy: <stage> error (<PythonException>) at line L, column C: <detail>"
// + the source snippet with a caret; runs twice (measure, then fill)
template <typename Sink>
constexpr void render_error(Sink & sink, const error_info_t & info, std::string_view text) noexcept {
	if (info.stage == error_stage::none) {
		return;
	}
	sink.put("ctpy: ");
	sink.put(to_string(info.stage));
	sink.put(" error (");
	sink.put(python_exception_name(info.kind));
	sink.put(") at line ");
	put_uint(sink, info.line);
	sink.put(", column ");
	put_uint(sink, info.column);
	sink.put(": ");
	sink.put(kind_detail(info.kind, info.position >= text.size()));
	render_snippet(sink, text, info.position);
}

// the rendered message in static storage; only instantiated on demand,
// and only for scripts that actually fail
template <ctll::fixed_string Src> struct message_storage {
	static constexpr std::size_t measure() noexcept {
		count_sink sink{};
		render_error(sink, error_info_of<Src>, source_text<Src>::view);
		return sink.at;
	}
	static constexpr std::size_t length = measure();
	struct out_t {
		char content[length + 1]{};
	};
	static constexpr out_t compute() noexcept {
		out_t out{};
		fill_sink sink{out.content};
		render_error(sink, error_info_of<Src>, source_text<Src>::view);
		return out;
	}
	static constexpr out_t content = compute();
	static constexpr std::string_view view{content.content, length};
};

} // namespace detail

// the structured failure report; never a hard error (a VALID script
// reports stage none / ok() == true). Negative tests should assert on
// these FIELDS, never on compiler output text.
CTPY_EXPORT template <ctll::fixed_string Src> constexpr error_info_t error_info() noexcept {
	return detail::error_info_of<Src>;
}

// the rendered caret diagnostic, in static storage; empty for a valid
// script, and never a hard error either
CTPY_EXPORT template <ctll::fixed_string Src> constexpr std::string_view error_message() noexcept {
	if constexpr (is_valid<Src>) {
		return std::string_view{};
	} else {
		return detail::message_storage<Src>::view;
	}
}

} // namespace ctpy

#endif
