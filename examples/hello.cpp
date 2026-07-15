#include <ctpy.hpp>
#include <iostream>

// the classic, executed entirely at compile time: the binary only
// carries the captured stdout

constexpr auto out = ctpy::run<R"py(
print("Hello from Python, at compile time!")
)py">();

static_assert(out.ok());
static_assert(out.stdout() == "Hello from Python, at compile time!\n");

int main() {
	std::cout << out.stdout();
}
