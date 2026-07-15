.PHONY: default all clean grammar regrammar pch single-header single-header/ctpy.hpp parity-cases

default: all

CXX_STANDARD := 20

PYTHON := python3

# LL1q parser generator: https://github.com/alexios-angel/Tablewright
# (needs python3 with the lark package). Not installed? Run it from a
# sibling checkout: make regrammar TABLEWRIGHT="PYTHONPATH=../tablewright python3 -m tablewright"
TABLEWRIGHT ?= tablewright

# a constexpr Python interpreter needs more evaluation budget than the
# defaults - the parse is linear (q)LL(1), but the tree-walk burns steps
CXX_IS_CLANG := $(shell $(CXX) --version 2>/dev/null | grep -qi clang && echo yes)
ifeq ($(CXX_IS_CLANG),yes)
CONSTEXPR_FLAGS := -fconstexpr-steps=500000000 -fconstexpr-depth=1024 -fbracket-depth=2048
else
CONSTEXPR_FLAGS := -fconstexpr-ops-limit=3000000000 -fconstexpr-loop-limit=10000000 -fconstexpr-depth=1024
endif

override CXXFLAGS := $(CXXFLAGS) -std=c++$(CXX_STANDARD) -Iinclude $(CONSTEXPR_FLAGS) -O2 -pedantic -Wall -Wextra -Werror -Wconversion

# precompiled header: ctll, ctc, the python_grammar table and the
# library templates are parsed once here instead of once per TU
ifeq ($(CXX_IS_CLANG),yes)
PCH := ctpy.pch
PCH_USE = -include-pch $(PCH)
else
PCH := include/ctpy.hpp.gch
PCH_USE =
endif

TESTS := $(wildcard tests/*.cpp)
OBJECTS := $(TESTS:%.cpp=%.o)
DEPENDENCY_FILES := $(OBJECTS:%.o=%.d)

all: $(OBJECTS)

$(OBJECTS): %.o: %.cpp $(PCH)
	$(CXX) $(CXXFLAGS) $(PCH_USE) -MMD -c $< -o $@

pch: $(PCH)

$(PCH): include/ctpy.hpp
	$(CXX) $(CXXFLAGS) -x c++-header $< -o $@

-include $(DEPENDENCY_FILES)

clean:
	rm -f $(OBJECTS) $(DEPENDENCY_FILES) ctpy.pch include/ctpy.hpp.gch

grammar: include/ctpy/python.hpp

regrammar:
	@rm -f include/ctpy/python.hpp
	@$(MAKE) grammar

include/ctpy/python.hpp: include/ctpy/python.lark
	@echo "LL1q $<"
	@$(TABLEWRIGHT) --ll --q --lang=lark --input=include/ctpy/python.lark --output=include/ctpy/ --generator=cpp_ctll_v2 --fname=python.hpp --namespace=ctpy --guard=CTPY__PYTHON__HPP --grammar-name=python_grammar

# regenerate tests/parity_cases.inc by running the snippets under the
# system python3 (offline dev tool; the checked-in file keeps CI hermetic)
parity-cases:
	$(PYTHON) tests/gen_expected.py

# needs python3 with the quom package
single-header: single-header/ctpy.hpp

single-header/ctpy.hpp:
	$(PYTHON) -m quom include/ctpy.hpp ctpy.hpp.tmp
	echo "/*" > single-header/ctpy.hpp
	cat LICENSE >> single-header/ctpy.hpp
	echo "*/" >> single-header/ctpy.hpp
	cat ctpy.hpp.tmp >> single-header/ctpy.hpp
	rm ctpy.hpp.tmp
