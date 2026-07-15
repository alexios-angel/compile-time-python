#ifndef CTPY__RESULT__HPP
#define CTPY__RESULT__HPP

#include "version.hpp"
#include "object.hpp"
#include "parse.hpp"
#include "eval.hpp"
#include "builtins.hpp"
#include "fstring.hpp"
#include "exec.hpp"
#include "views.hpp"
#include "../ctc.hpp"

#ifndef CTPY_IN_A_MODULE
#include <cstddef>
#include <cstdint>
#include <string_view>
#endif

// From a dead interpreter State to a durable result. The arena is
// append-only and full of garbage (realloc'd runs, call temporaries,
// locals), so a FLATTENING pass copies exactly the reachable data -
// every global, plus eval<>'s expression value - into fresh dense
// pools, preserving the index-range representation the views
// (views.hpp) navigate. For a seed-free run the flattened pools are
// then right-sized with ctc::shrunk into a per-Src static constexpr
// result (`stored_run`, evaluated once per Src no matter how many
// static_asserts read it); a seeded run's sizes depend on the seed
// VALUES, which a function argument can never surface at type level,
// so those results keep the oversized capacities (document-sized, not
// arena-sized, once the user right-sizes lifted pieces themselves).
//
//   constexpr auto out = ctpy::run<R"py(
//   answer = 6 * 7
//   print("the answer is", answer)
//   )py">();
//   static_assert(out.ok());
//   static_assert(out.stdout() == "the answer is 42\n");
//   static_assert(out["answer"].to<int>() == 42);
//
// Family policy throughout: a NON-PARSING source hard-errors (the
// static_assert names the stage; soft-check with ctpy::is_valid<Src>),
// a RAISING script is a value - ok() is false and exception() carries
// the type/message/line.

namespace ctpy {

// the capacities of one flattened result (structural, so a single NTTP
// shapes a run_result)
CTPY_EXPORT struct result_caps {
	std::size_t objs = 0;
	std::size_t chars = 0;
	std::size_t pairs = 0;
	std::size_t globals = 0;
	std::size_t out = 0;
};

// The result of ctpy::run<Src>(...) / ctpy::eval<Src>() /
// ctpy::module<Src>.call<Fn>(...): the flattened pools as plain public
// data (a result bound to a constexpr variable persists to runtime
// as-is), the captured stdout, the raised-exception channel, and the
// accessors that hand out ctpy::value views pointing into those pools.
CTPY_EXPORT template <result_caps Caps> struct run_result {
	ctc::vector<Object, Caps.objs> objs{};
	ctc::vector<char, Caps.chars> chars{};
	ctc::vector<Pair, Caps.pairs> pairs{};
	ctc::vector<flat_global, Caps.globals> bindings{};
	ctc::string<Caps.out> stdout_text{};
	std::uint32_t result_obj = not_found; // eval<>/call<>'s value; not_found for run<>
	Kind kind = Kind::none;               // ... and its Kind (none for run<>)
	bool raised = false;
	PyError error{};

	// did the script finish without raising? (a raising script is NEVER
	// a build failure - the exception is a queryable value)
	constexpr bool ok() const noexcept {
		return !raised;
	}
	constexpr const PyError & exception() const noexcept {
		return error;
	}

	// everything print() wrote, viewed out of this result's storage
	constexpr std::string_view stdout() const noexcept {
		return stdout_text.view();
	}

	// a global by name; a name that was never bound yields the harmless
	// null-object view (exists() == false), which chains on quietly
	constexpr value operator[](std::string_view name) const noexcept {
		for (std::size_t at = 0; at < bindings.size(); ++at) {
			if (bindings[at].name() == name) {
				return make_value(objs.data(), pairs.data(), chars.data(),
				                  objs[bindings[at].obj]);
			}
		}
		return value{};
	}

	// every global, in binding order:  for (auto g : out.globals()) ...
	constexpr globals_range globals() const noexcept {
		return globals_range{
			globals_iterator{bindings.data(), objs.data(), pairs.data(), chars.data()},
			bindings.data() + bindings.size()};
	}

	// the value channel: eval<Src>'s expression result, call<Fn>'s
	// return value (the null view for a plain run<> or after a raise)
	constexpr value result() const noexcept {
		if (result_obj == not_found) {
			return value{};
		}
		return make_value(objs.data(), pairs.data(), chars.data(), objs[result_obj]);
	}
	constexpr std::string_view str() const noexcept {
		return result().str();
	}
	template <typename T> constexpr T to() const noexcept {
		return result().template to<T>();
	}
};

namespace detail {

// flattened capacities generous enough for ANY state of an ArenaT run:
// the memoized copy visits each reachable run once (<= the object
// pool), plus one standalone header per global and per dict key/value,
// plus the result value's header. str runs that overlap (materialized
// per-character strs share their parent's chars) can copy content
// twice, hence the chars margin. Right-sizing shrinks all of it away.
template <typename ArenaT> inline constexpr result_caps flat_caps = {
	ArenaT::objs_capacity + ArenaT::frames_capacity + 2 * ArenaT::pairs_capacity + 1,
	2 * ArenaT::chars_capacity,
	ArenaT::pairs_capacity,
	ArenaT::frames_capacity,
	ArenaT::out_capacity};

// the value-level flattening pass: a garbage-collecting deep copy of
// the reachable objects out of a (dead) State into a run_result's
// dense pools. Runs are memoized by their origin (first, count), so a
// run shared by many holders is copied once and stays shared; v0.1
// data can never be cyclic (displays and append copy element OBJECTS),
// so plain recursion terminates at the nesting depth of the data.
template <result_caps Caps, typename StateT> struct flattener {
	const StateT & st;
	run_result<Caps> & out;
	// memo: origin run first -> flat run first + 1 (0 = unset), keyed
	// with the run length so only exact matches are reused
	std::uint32_t obj_runs[StateT::arena_type::objs_capacity]{};
	std::uint32_t obj_lens[StateT::arena_type::objs_capacity]{};
	std::uint32_t char_runs[StateT::arena_type::chars_capacity]{};
	std::uint32_t char_lens[StateT::arena_type::chars_capacity]{};
	std::uint32_t pair_runs[StateT::arena_type::pairs_capacity]{};
	std::uint32_t pair_lens[StateT::arena_type::pairs_capacity]{};

	// append a standalone header object, returning its flat index
	constexpr std::uint32_t push(const Object & header) {
		const std::uint32_t at = static_cast<std::uint32_t>(out.objs.size());
		out.objs.push_back(header);
		return at;
	}

	constexpr std::uint32_t copy_chars(std::uint32_t first, std::uint32_t count) {
		if (count == 0) {
			return 0;
		}
		if (char_runs[first] != 0 && char_lens[first] == count) {
			return char_runs[first] - 1;
		}
		const std::uint32_t flat = static_cast<std::uint32_t>(out.chars.size());
		char_runs[first] = flat + 1;
		char_lens[first] = count;
		for (std::uint32_t at = 0; at < count; ++at) {
			out.chars.push_back(st.a.chars[first + at]);
		}
		return flat;
	}

	constexpr std::uint32_t copy_objs(std::uint32_t first, std::uint32_t count) {
		if (count == 0) {
			return 0;
		}
		if (obj_runs[first] != 0 && obj_lens[first] == count) {
			return obj_runs[first] - 1;
		}
		const std::uint32_t flat = static_cast<std::uint32_t>(out.objs.size());
		obj_runs[first] = flat + 1;
		obj_lens[first] = count;
		// reserve the whole run first so it stays contiguous while the
		// elements' own payloads land behind it
		out.objs.resize(flat + count);
		for (std::uint32_t at = 0; at < count; ++at) {
			out.objs[flat + at] = copy(st.a.objs[first + at]);
		}
		return flat;
	}

	constexpr std::uint32_t copy_pairs(std::uint32_t first, std::uint32_t count) {
		if (count == 0) {
			return 0;
		}
		if (pair_runs[first] != 0 && pair_lens[first] == count) {
			return pair_runs[first] - 1;
		}
		const std::uint32_t flat = static_cast<std::uint32_t>(out.pairs.size());
		pair_runs[first] = flat + 1;
		pair_lens[first] = count;
		out.pairs.resize(flat + count);
		for (std::uint32_t at = 0; at < count; ++at) {
			const Pair entry = st.a.pairs[first + at];
			const std::uint32_t key = push(copy(st.a.objs[entry.key]));
			const std::uint32_t val = push(copy(st.a.objs[entry.value]));
			out.pairs[flat + at] = Pair{key, val};
		}
		return flat;
	}

	// the flattened HEADER of one object (containers pull their runs in)
	constexpr Object copy(const Object & src) {
		switch (src.kind) {
			case Kind::none:
			case Kind::boolean:
			case Kind::int_:
			case Kind::float_:
				return Object{.kind = src.kind, .i = src.i, .f = src.f};
			case Kind::function:
				// opaque outside the interpreter: thunk index and the
				// defaults run mean nothing to a view, keep the kind only
				return Object{.kind = Kind::function};
			case Kind::str:
			case Kind::file:
				return Object{.kind = src.kind,
				              .first = copy_chars(src.first, src.count),
				              .count = src.count};
			case Kind::range: // its run is the three start/stop/step ints
			case Kind::tuple:
			case Kind::list:
			case Kind::set:
				return Object{.kind = src.kind,
				              .first = copy_objs(src.first, src.count),
				              .count = src.count};
			case Kind::dict:
				return Object{.kind = Kind::dict,
				              .first = copy_pairs(src.first, src.count),
				              .count = src.count};
		}
		return Object{};
	}
};

template <result_caps Caps, typename StateT>
constexpr run_result<Caps> flatten(const StateT & st, std::uint32_t result_index) {
	run_result<Caps> out{};
	out.raised = st.raised;
	out.error = st.error;
	for (std::size_t at = 0; at < st.a.out.size(); ++at) {
		out.stdout_text.push_back(st.a.out[at]);
	}
	flattener<Caps, StateT> pass{st, out};
	for (std::uint32_t at = 0; at < st.globals_count; ++at) {
		const Binding & binding = st.a.frames[at];
		const std::uint32_t header = pass.push(pass.copy(st.a.objs[binding.obj]));
		out.bindings.push_back(flat_global{binding.name.data(),
		                                   static_cast<std::uint32_t>(binding.name.size()),
		                                   header});
	}
	if (result_index != not_found) {
		const Object header = pass.copy(st.a.objs[result_index]);
		out.result_obj = pass.push(header);
		out.kind = header.kind;
	}
	return out;
}

// --- executing a source into a flat (still oversized) result ------------------

// seed descriptors (bind.hpp: arg<>/file<>/stdin_text<>/pymodule<>)
// bind left to right, then the module body runs
template <ctll::fixed_string Src, typename ArenaT, typename... Seeds>
constexpr auto run_to_flat(const Seeds &... seeds) {
	static_assert(require_valid<Src>()); // hard-errors NAMING the stage
	State<ArenaT> st{};
	st.line_map = prelex_raw<Src>.lines.data();
	st.line_map_count = static_cast<std::uint32_t>(prelex_raw<Src>.lines.size());
	(seeds.seed(st), ...);
	(void)exec_node<parsed_module<Src>, State<ArenaT>>(st);
	return flatten<flat_caps<ArenaT>>(st, not_found);
}

template <ctll::fixed_string Src, typename ArenaT>
constexpr auto eval_to_flat() {
	static_assert(require_valid<Src>()); // hard-errors NAMING the stage
	using Expr = typename single_expr<parsed_module<Src>>::type;
	State<ArenaT> st{};
	st.line_map = prelex_raw<Src>.lines.data();
	st.line_map_count = static_cast<std::uint32_t>(prelex_raw<Src>.lines.size());
	st.current_line = st.line_at(0); // the whole source is one expression
	const std::uint32_t result = eval_node<Expr, State<ArenaT>>(st);
	return flatten<flat_caps<ArenaT>>(st, st.raised ? not_found : result);
}

// evaluated ONCE per Src (variable templates memoize), however many
// static_asserts read the result
template <ctll::fixed_string Src, typename ArenaT>
inline constexpr auto oversized_run = run_to_flat<Src, ArenaT>();

template <ctll::fixed_string Src, typename ArenaT>
inline constexpr auto oversized_eval = eval_to_flat<Src, ArenaT>();

// --- right-sizing: the oversized pools through ctc::shrunk ---------------------

template <std::size_t Objs, std::size_t Chars, std::size_t Pairs, std::size_t Globals, std::size_t Out>
constexpr run_result<result_caps{Objs, Chars, Pairs, Globals, Out}> reassemble(
		const ctc::vector<Object, Objs> & objs,
		const ctc::vector<char, Chars> & chars,
		const ctc::vector<Pair, Pairs> & pairs,
		const ctc::vector<flat_global, Globals> & bindings,
		const ctc::basic_string<char, Out> & stdout_text,
		std::uint32_t result_obj, Kind kind, bool raised, const PyError & error) {
	run_result<result_caps{Objs, Chars, Pairs, Globals, Out}> out{};
	out.objs = objs;
	out.chars = chars;
	out.pairs = pairs;
	out.bindings = bindings;
	out.stdout_text = stdout_text;
	out.result_obj = result_obj;
	out.kind = kind;
	out.raised = raised;
	out.error = error;
	return out;
}

// a copy of Big whose every pool holds exactly what it uses; indices
// survive unchanged because shrunk only trims capacity, never content
template <const auto & Big>
inline constexpr auto shrunk_result = reassemble(
	ctc::shrunk<Big.objs>(),
	ctc::shrunk<Big.chars>(),
	ctc::shrunk<Big.pairs>(),
	ctc::shrunk<Big.bindings>(),
	ctc::shrunk<Big.stdout_text>(),
	Big.result_obj, Big.kind, Big.raised, Big.error);

// the per-Src static constexpr results the public entry points hand out
template <ctll::fixed_string Src, typename ArenaT>
inline constexpr auto stored_run = shrunk_result<oversized_run<Src, ArenaT>>;

template <ctll::fixed_string Src, typename ArenaT>
inline constexpr auto stored_eval = shrunk_result<oversized_eval<Src, ArenaT>>;

} // namespace detail

// Run a Python module at compile time.
//
//   constexpr auto out = ctpy::run<"total = 0\nfor i in range(5):\n    total += i\n">();
//   static_assert(out.ok() && out["total"].to<int>() == 10);
//
// Arguments seed the script before it runs (bind.hpp descriptors):
//
//   ctpy::run<"total = sum(values) * factor\n">(
//       ctpy::arg<"values">(ctc::vector<int, 3>{1, 2, 3}),
//       ctpy::arg<"factor">(10));
//
// A seed-free run comes back right-sized out of per-Src static
// storage; a seeded run keeps oversized pools (its sizes depend on the
// seed values, which no function argument can lift to type level). A
// script that overflows the default Arena capacities can pass a bigger
// one: run<Src, Arena<8192, 32768>>().
CTPY_EXPORT template <ctll::fixed_string Src, typename ArenaT = Arena<>, typename... Seeds>
constexpr auto run(const Seeds &... seeds) noexcept {
	if constexpr (sizeof...(Seeds) == 0) {
		return detail::stored_run<Src, ArenaT>;
	} else {
		return detail::run_to_flat<Src, ArenaT>(seeds...);
	}
}

// sugar: evaluate one Python EXPRESSION at compile time.
//
//   static_assert(ctpy::eval<"2 ** 10">().to<int>() == 1024);
//   static_assert(ctpy::eval<"'py' * 2">().str() == "pypy");
//
// The expression's value is the result() channel, with to<T>()/str()
// forwarded on the result itself; statements need run<Src>.
CTPY_EXPORT template <ctll::fixed_string Src, typename ArenaT = Arena<>>
constexpr auto eval() noexcept {
	return detail::stored_eval<Src, ArenaT>;
}

} // namespace ctpy

#endif
