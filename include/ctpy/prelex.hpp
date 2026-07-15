#ifndef CTPY__PRELEX__HPP
#define CTPY__PRELEX__HPP

#include "version.hpp"
#include "../ctc.hpp"
#include "../ctll/fixed_string.hpp"

#ifndef CTPY_IN_A_MODULE
#include <cstddef>
#include <cstdint>
#endif

// The pre-lexer. Python's grammar is context-free only after a stateful
// tokenizer has turned significant whitespace into INDENT/DEDENT tokens;
// a character-level (q)LL(1) table cannot do that. So before parsing,
// the raw script is rewritten into an equivalent stream where structure
// is explicit single characters the grammar can match directly:
//
//   \x01  INDENT      the line starts a deeper block
//   \x02  DEDENT      a block ended (one marker per level closed)
//   \x03  NEWLINE     a logical line ended
//   \x04  ENDMARKER   end of input (after the final DEDENT flush)
//
// In CPython tokenizer order: string literals are protected FIRST (all
// quote forms, any r/b/f/u prefix combination, triple-quoted with their
// real newlines - contents are copied verbatim, so a '#' or a bracket
// inside a string never confuses the passes below); comments are
// stripped; a backslash before a newline and any newline inside open
// ( [ { brackets continue the logical line (the newline becomes a plain
// space); blank and comment-only lines vanish entirely; a tab advances
// the indentation column to the next multiple of 8; EOF flushes the
// whole indent stack as DEDENTs and appends ENDMARKER.
//
// A script that does not pre-lex (a dedent to a depth that was never
// indented to, an unterminated string) must not hard-error here - the
// family promise is that only run<> fails the build, while is_valid<> /
// error_message<>() stay soft. So failures land in a queryable status:
//
//   static_assert(!ctpy::prelex_ok<"if x:\n        a\n    b\n">);
//   static_assert(ctpy::prelex_diag<...>.kind
//                 == ctpy::prelex_error_kind::inconsistent_dedent);
//
// The output length is only known after the rewrite, so the pass runs
// the ctc oversize-then-shrink two-step: build into a generous
// ctc::string (bound 2*len+4: content + one NEWLINE and INDENT per
// line, DEDENTs bounded by INDENTs, plus ENDMARKER), right-size with
// ctc::shrunk, then hand the result to the parser as a ctll NTTP:
//
//   raw source        ctll::fixed_string NTTP  (what run<> receives)
//   -> prelex<Src>()  ctc::string<2n+4> + status   (oversized pass)
//   -> prelexed<Src>  ctc::string<exact>           (ctc::shrunk)
//   -> prelexed_fixed<Src> ctll::fixed_string      (to_fixed_string)

namespace ctpy {

using std::size_t;

// the marker characters (control codes no real script contains)
CTPY_EXPORT inline constexpr char indent_marker = '\x01';
CTPY_EXPORT inline constexpr char dedent_marker = '\x02';
CTPY_EXPORT inline constexpr char newline_marker = '\x03';
CTPY_EXPORT inline constexpr char endmarker = '\x04';

// why a pre-lex failed (none = it succeeded)
CTPY_EXPORT enum class prelex_error_kind : unsigned char {
	none,                // the rewrite succeeded
	inconsistent_dedent, // a dedent to a column that is not on the indent stack
	unterminated_string, // a string literal never closed (EOL or EOF inside it)
	too_deep             // more nested blocks than the indent stack can hold
};

CTPY_EXPORT constexpr const char * to_string(prelex_error_kind k) noexcept {
	switch (k) {
		case prelex_error_kind::none: return "none";
		case prelex_error_kind::inconsistent_dedent: return "inconsistent dedent";
		case prelex_error_kind::unterminated_string: return "unterminated string";
		case prelex_error_kind::too_deep: return "indentation too deep";
	}
	return "unknown";
}

// the queryable error state: what failed, on which 1-based source line,
// and at which byte offset into the RAW source (line 0 / offset 0 when
// nothing failed; diag.hpp turns the offset into line/column + caret)
CTPY_EXPORT struct prelex_status {
	prelex_error_kind kind = prelex_error_kind::none;
	size_t line = 0;
	size_t offset = 0;
};

// What the oversized pass produces: the marker stream so far plus the
// status (on failure the text is the partial rewrite, for debugging),
// and two side tables the diagnostics/traceback machinery reads:
//
//   src_map[k]  the RAW-source byte offset that produced text[k] (for
//               synthesized markers: the offset they stand for), so a
//               parse failure at marker position k maps back to a
//               caret position in the original script;
//   lines[k]    the 1-based PHYSICAL source line on which LOGICAL line
//               k started - the traceback side of the line threading:
//               the parse actions stamp each statement with its logical
//               line ordinal (ast::lined<N, Stmt>), and the interpreter
//               resolves ordinals through this table, so statements
//               continued across physical lines (backslash, brackets)
//               report the line they started on, like CPython.
CTPY_EXPORT template <size_t N> struct prelex_result {
	ctc::string<N> text{};
	ctc::vector<std::uint32_t, N> src_map{};
	ctc::vector<std::uint32_t, N / 2 + 2> lines{};
	prelex_status status{};

	constexpr bool ok() const noexcept {
		return status.kind == prelex_error_kind::none;
	}
};

namespace detail {

// deeper than any sane script (CPython's own limit is 100)
inline constexpr size_t prelex_max_indent = 100;

template <size_t Cap> struct prelexer {
	const char * in;
	size_t n;
	prelex_result<Cap> out{};
	size_t indents[prelex_max_indent] = {};
	size_t sp{1};    // indent stack pointer; indents[0] = 0 is always there
	size_t depth{0}; // open ( [ { bracket depth
	size_t line{1};  // 1-based physical line, for the error state
	size_t i{0};

	constexpr bool failed() const noexcept {
		return out.status.kind != prelex_error_kind::none;
	}
	constexpr void fail(prelex_error_kind kind, size_t at_line, size_t at_offset) noexcept {
		out.status.kind = kind;
		out.status.line = at_line;
		out.status.offset = at_offset;
	}
	// emit one output unit, remembering which source offset produced it
	// (synthesized markers pass the offset they stand for)
	constexpr void put_at(char c, size_t from) noexcept {
		out.text.push_back(c);
		out.src_map.push_back(static_cast<std::uint32_t>(from));
	}
	constexpr void put(char c) noexcept {
		put_at(c, i);
	}

	// measure the indentation of the physical line starting at `i` and
	// emit INDENT/DEDENT markers; returns false when the line carries no
	// code (blank or comment-only - such lines vanish) or on failure
	constexpr bool handle_indentation() noexcept {
		size_t col = 0;
		while (i < n && (in[i] == ' ' || in[i] == '\t' || in[i] == '\f' || in[i] == '\r')) {
			if (in[i] == '\t') {
				col = (col / 8 + 1) * 8; // tab: next multiple of 8
			} else if (in[i] == ' ') {
				++col;
			}
			++i;
		}
		if (i >= n) {
			return false;
		}
		if (in[i] == '\n') { // blank line: emits nothing at all
			++i;
			++line;
			return false;
		}
		if (in[i] == '#') { // comment-only line: same
			while (i < n && in[i] != '\n') {
				++i;
			}
			if (i < n) {
				++i;
				++line;
			}
			return false;
		}
		if (col > indents[sp - 1]) {
			if (sp == prelex_max_indent) {
				fail(prelex_error_kind::too_deep, line, i);
				return false;
			}
			indents[sp] = col;
			++sp;
			put(indent_marker);
		} else {
			while (sp > 1 && col < indents[sp - 1]) {
				--sp;
				put(dedent_marker);
			}
			if (col != indents[sp - 1]) {
				fail(prelex_error_kind::inconsistent_dedent, line, i);
				return false;
			}
		}
		return true;
	}

	// copy one string literal verbatim, `i` on its opening quote (any
	// prefix letters were already copied as ordinary characters - raw
	// and cooked strings scan identically, since a backslash always
	// shields the next character from terminating the literal)
	constexpr void scan_string() noexcept {
		const char quote = in[i];
		const size_t start_line = line;
		const size_t start_at = i; // the opening quote, for the error caret
		put(quote);
		++i;
		bool triple = false;
		if (i + 1 < n && in[i] == quote && in[i + 1] == quote) {
			triple = true;
			put(quote);
			put(quote);
			i += 2;
		}
		while (i < n) {
			const char c = in[i];
			if (c == '\\') { // escape: copy both units, whatever the second is
				put(c);
				++i;
				if (i < n) {
					if (in[i] == '\n') {
						++line;
					}
					put(in[i]);
					++i;
				}
				continue;
			}
			if (c == '\n') {
				if (!triple) {
					fail(prelex_error_kind::unterminated_string, start_line, start_at);
					return;
				}
				++line; // triple-quoted: real newlines stay in
				put(c);
				++i;
				continue;
			}
			if (c == quote) {
				if (!triple) {
					put(c);
					++i;
					return;
				}
				if (i + 2 < n && in[i + 1] == quote && in[i + 2] == quote) {
					put(quote);
					put(quote);
					put(quote);
					i += 3;
					return;
				}
				put(c); // a lone quote inside a triple-quoted literal
				++i;
				continue;
			}
			put(c);
			++i;
		}
		fail(prelex_error_kind::unterminated_string, start_line, start_at);
	}

	// copy the rest of the logical line (which can span physical lines
	// via backslash or bracket continuation); ends after emitting the
	// NEWLINE marker, at input end, or on failure
	constexpr void scan_line() noexcept {
		while (i < n) {
			const char c = in[i];
			if (c == '\n') {
				++line;
				++i;
				if (depth > 0) { // implicit continuation inside ( [ {
					put_at(' ', i - 1);
					continue;
				}
				put_at(newline_marker, i - 1);
				return;
			}
			if (c == '\\' && i + 1 < n && in[i + 1] == '\n') { // explicit join
				++line;
				i += 2;
				put_at(' ', i - 2);
				continue;
			}
			if (c == '\\' && i + 2 < n && in[i + 1] == '\r' && in[i + 2] == '\n') {
				++line;
				i += 3;
				put_at(' ', i - 3);
				continue;
			}
			if (c == '#') { // comment: gone to end of physical line
				while (i < n && in[i] != '\n') {
					++i;
				}
				continue;
			}
			if (c == '\'' || c == '"') {
				scan_string();
				if (failed()) {
					return;
				}
				continue;
			}
			if (c == '(' || c == '[' || c == '{') {
				++depth;
			} else if (c == ')' || c == ']' || c == '}') {
				if (depth > 0) {
					--depth;
				}
			}
			if (c != '\r') {
				put(c);
			}
			++i;
		}
		put(newline_marker); // EOF ends the last logical line
	}

	constexpr void run() noexcept {
		while (i < n && !failed()) {
			if (!handle_indentation()) {
				continue;
			}
			// a new LOGICAL line starts here: remember its physical line
			out.lines.push_back(static_cast<std::uint32_t>(line));
			scan_line();
		}
		if (failed()) {
			return;
		}
		while (sp > 1) { // EOF closes every open block
			--sp;
			put(dedent_marker);
		}
		put(endmarker);
	}
};

} // namespace detail

// the oversized pass: raw source -> marker stream + status, in a buffer
// generous enough for any input (right-size the .text with ctc::shrunk,
// or just use prelexed<Src> below)
CTPY_EXPORT template <ctll::fixed_string Src> consteval auto prelex() noexcept {
	constexpr size_t n = Src.size();
	// ctll::fixed_string holds code points; a char source stores bytes
	// 1:1, so narrowing back is lossless for any byte-based literal
	char in[n ? n : 1] = {};
	for (size_t k = 0; k < n; ++k) {
		in[k] = static_cast<char>(static_cast<unsigned char>(Src[k]));
	}
	detail::prelexer<2 * n + 4> p{in, n};
	p.run();
	return p.out;
}

// a ctc string NTTP re-encoded as a ctll::fixed_string (what CTLL's
// parser takes as its subject NTTP)
CTPY_EXPORT template <auto S> consteval auto to_fixed_string() noexcept {
	ctll::fixed_string<S.size()> result{};
	for (size_t k = 0; k < S.size(); ++k) {
		result.content[k] = static_cast<char32_t>(static_cast<unsigned char>(S[k]));
	}
	result.real_size = S.size();
	return result;
}

// the oversized pass, evaluated once per Src
CTPY_EXPORT template <ctll::fixed_string Src> inline constexpr auto prelex_raw = prelex<Src>();

// the queryable error state (kind + 1-based line; kind == none on success)
CTPY_EXPORT template <ctll::fixed_string Src> inline constexpr prelex_status prelex_diag = prelex_raw<Src>.status;

// did the source pre-lex cleanly?
CTPY_EXPORT template <ctll::fixed_string Src> inline constexpr bool prelex_ok = prelex_diag<Src>.kind == prelex_error_kind::none;

// the marker stream, right-sized to exactly its length
CTPY_EXPORT template <ctll::fixed_string Src> inline constexpr auto prelexed = ctc::shrunk<prelex_raw<Src>.text>();

// the full handoff: the marker stream as a ctll::fixed_string NTTP
CTPY_EXPORT template <ctll::fixed_string Src> inline constexpr auto prelexed_fixed = to_fixed_string<prelexed<Src>>();

} // namespace ctpy

#endif
