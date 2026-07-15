#include <ctpy.hpp>
#include <iostream>

// the C++ <-> Python boundary, both directions: C++ values seed the
// script's globals (arg<>), a mounted compile-time file feeds open(),
// and the results lift back out into ctc containers

constexpr auto out = ctpy::run<R"py(
raw = open("limits.conf").read()
scaled = []
for entry in raw.split():
    scaled.append(int(entry) * factor)
total = sum(scaled)
)py">(ctpy::arg<"factor">(10),
      ctpy::file<"limits.conf", "1 2 3 4\n">);

static_assert(out.ok());
static_assert(out["total"].to<int>() == 100);

// lift the Python list into a ctc vector, right-sized for static storage
constexpr auto scaled = ctpy::lift<ctc::vector<int, 16>>(out["scaled"]);
static_assert(scaled.size() == 4 && scaled[3] == 40);

int main() {
	std::cout << "scaled:";
	for (int value : scaled) {
		std::cout << ' ' << value;
	}
	std::cout << "\ntotal: " << out["total"].to<int>() << '\n';
}
