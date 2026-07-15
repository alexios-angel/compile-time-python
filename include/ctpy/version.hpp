#ifndef CTPY__VERSION__HPP
#define CTPY__VERSION__HPP

#ifdef CTPY_IN_A_MODULE
#define CTPY_EXPORT export
#else
#define CTPY_EXPORT
#endif

namespace ctpy {

CTPY_EXPORT inline constexpr int version_major = 0;
CTPY_EXPORT inline constexpr int version_minor = 1;
CTPY_EXPORT inline constexpr int version_patch = 0;

} // namespace ctpy

#endif
