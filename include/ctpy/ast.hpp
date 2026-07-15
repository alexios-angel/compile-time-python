#ifndef CTPY__AST__HPP
#define CTPY__AST__HPP

#include "version.hpp"
#include "text.hpp"

// The type-level AST of a Python script, built by the semantic actions
// (actions.hpp) while CTLL drives the generated (q)LL(1) table
// (python.hpp) over the pre-lexed source. Shapes follow CPython's ast
// module where practical: Compare carries a chain of (op, operand)
// links, Assign carries all targets of a chained assignment, an
// if-statement owns its elif clauses and optional else suite.
//
// Every node is an empty struct - the whole parse result is a TYPE.
// The interpreter (M3+) walks these with value-level State; literal
// nodes carry their raw source text (escape cooking and number
// parsing happen at evaluation, where a value-level pass is cheap).
// `void` fills optional slots that are absent (a slice with no step,
// an if with no else, a return with no value, a param without a
// default).

namespace ctpy::ast {

// --- operator tags

// binary
struct op_or { };
struct op_and { };
struct op_bor { };
struct op_bxor { };
struct op_band { };
struct op_shl { };
struct op_shr { };
struct op_add { };
struct op_sub { };
struct op_mul { };
struct op_div { };
struct op_floordiv { };
struct op_mod { };
struct op_pow { };
// comparison (chainable)
struct op_lt { };
struct op_le { };
struct op_gt { };
struct op_ge { };
struct op_eq { };
struct op_ne { };
struct op_in { };
struct op_not_in { };
struct op_is { };
struct op_is_not { };
// unary
struct op_not { };
struct op_neg { };
struct op_pos { };
struct op_invert { };

// --- expressions

template <typename Text> struct name { using text = Text; };
template <typename Text> struct int_lit { using text = Text; };
template <typename Text> struct float_lit { using text = Text; };
template <typename Text> struct str_lit { using text = Text; };
// an f-string, body kept raw ({expr} holes are parsed at M7)
template <typename Text> struct fstr_lit { using text = Text; };
struct none_lit { };
struct true_lit { };
struct false_lit { };

template <typename... Es> struct tuple_expr { };
template <typename... Es> struct list_expr { };
template <typename... Es> struct set_expr { };
template <typename K, typename V> struct dict_item { };
template <typename... Items> struct dict_expr { };

template <typename Op, typename E> struct unary_expr { };
template <typename Op, typename L, typename R> struct binary_expr { };
// one link of a chained comparison: OP followed by its right operand
template <typename Op, typename R> struct cmp_link { };
// a < b < c  ==  compare_expr<a, cmp_link<lt, b>, cmp_link<lt, c>>
template <typename L, typename... Links> struct compare_expr { };
// Then if Cond else Else
template <typename Cond, typename Then, typename Else> struct ternary_expr { };

// name=value in a call (and, reused, a defaulted parameter)
template <typename NameText, typename V> struct kwarg { };
template <typename Fn, typename... Args> struct call_expr { };
template <typename Obj, typename Index> struct subscript_expr { };
// a[L:U:S]; absent parts are void
template <typename L, typename U, typename S> struct slice_expr { };
template <typename Obj, typename NameText> struct attribute_expr { };

// --- statements

// The traceback line threading (M9). Every statement that lands in a
// suite / module / clause_pack is wrapped in lined<N, Stmt>, where N is
// the 0-based LOGICAL line ordinal the statement STARTED on (an elif
// clause is wrapped the same way - its test has its own line). The
// ordinal is counted by the parse actions from the pre-lexer's NEWLINE
// markers (a type-level counter at the bottom of the parse stack,
// bumped by the grammar's @bump_line hooks), and the interpreter
// resolves it to a 1-based physical source line through the pre-lexer's
// logical-line table (prelex_result::lines) - so a statement continued
// across physical lines (backslash, brackets) reports the line it
// started on, exactly like a CPython traceback. Executing lined<N, S>
// stamps State::current_line before running S; raise_error copies it
// into PyError::line. Granularity is deliberately per-STATEMENT: an
// expression inside a multi-line statement reports the statement's
// first line (documented v0.1 deviation).
template <unsigned N, typename S> struct lined { };

template <typename E> struct expr_stmt { };
// x = y = V  ==  assign_stmt<V, x, y>  (targets in source order)
template <typename V, typename... Targets> struct assign_stmt { };
template <typename Op, typename Target, typename V> struct aug_stmt { };
struct pass_stmt { };
struct break_stmt { };
struct continue_stmt { };
// E is void for a bare "return"
template <typename E> struct return_stmt { };

template <typename... Stmts> struct suite { };
template <typename Test, typename Body> struct elif_clause { };
template <typename... Clauses> struct clause_pack { };
// Else is a suite, or void while the statement can still grow an
// elif/else clause (the actions attach clauses to the top of stack)
template <typename Test, typename Body, typename Elifs, typename Else> struct if_stmt { };
template <typename Test, typename Body, typename Else> struct while_stmt { };
template <typename Target, typename Iter, typename Body, typename Else> struct for_stmt { };

// Default is void for a plain positional parameter
template <typename NameText, typename Default> struct param { };
template <typename... Params> struct param_pack { };
template <typename NameText, typename Params, typename Body> struct def_stmt { };

template <typename... Stmts> struct module { };

} // namespace ctpy::ast

#endif
