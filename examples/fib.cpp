#include <ctpy.hpp>
#include <iostream>

// functions, loops, recursion - and the results are constants

constexpr auto out = ctpy::run<R"py(
def fib(n):
    a, b = 0, 1
    for _ in range(n):
        a, b = b, a + b
    return a

answer = fib(10)
print("fib(10) =", answer)
)py">();

static_assert(out.ok());
static_assert(out.stdout() == "fib(10) = 55\n");
static_assert(out["answer"].to<int>() == 55);

// the Python result is a genuine compile-time constant: use it as an
// array bound, a template argument, anything
int lookup_table[out["answer"].to<int>()] = {};

int main() {
	std::cout << out.stdout();
	std::cout << "sizeof(lookup_table) = " << sizeof(lookup_table) / sizeof(int) << '\n';
}
