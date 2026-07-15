#!/usr/bin/env python3
"""Guard-aware single-header amalgamator.

quom 4.0.2 fails to deduplicate this repo's include graph (it re-inlines
headers until the output runs away — the same failure notre documents).
This does the one thing needed: recursively inline every `#include "..."`
exactly once per resolved path, keep `<system>` includes as-is, and drop
the INLINED files' include guards (each file is emitted once, so they are
dead weight in the amalgam). The ENTRY header keeps its guard: everything
inlines inside it, so it becomes the whole amalgam's include guard.

Usage: tools/amalgamate.py <entry-header> <output> [-I include-dir]
"""

import argparse
import re
import sys
from pathlib import Path

LOCAL_INCLUDE = re.compile(r'^\s*#\s*include\s*"([^"]+)"\s*$')
SYSTEM_INCLUDE = re.compile(r'^\s*#\s*include\s*<([^>]+)>\s*$')


def amalgamate(entry: Path, include_dirs: list[Path]) -> str:
    seen: set[Path] = set()
    out: list[str] = []

    def resolve(name: str, relative_to: Path) -> Path | None:
        candidate = (relative_to.parent / name).resolve()
        if candidate.is_file():
            return candidate
        for base in include_dirs:
            candidate = (base / name).resolve()
            if candidate.is_file():
                return candidate
        return None

    def emit(path: Path, keep_guard: bool = False) -> None:
        if path in seen:
            return
        seen.add(path)
        text = path.read_text(encoding="utf-8")
        lines = text.splitlines()
        # drop the include guard: the first `#ifndef X` immediately
        # followed by `#define X`, and the last `#endif` - except for the
        # entry header, whose guard must survive to guard the amalgam
        guard = None
        if not keep_guard and len(lines) >= 2:
            first = next((i for i, l in enumerate(lines) if l.strip()), None)
            if first is not None and first + 1 < len(lines):
                m1 = re.match(r"^\s*#\s*ifndef\s+(\w+)\s*$", lines[first])
                m2 = re.match(r"^\s*#\s*define\s+(\w+)\s*$", lines[first + 1])
                if m1 and m2 and m1.group(1) == m2.group(1):
                    guard = m1.group(1)
                    lines[first] = ""
                    lines[first + 1] = ""
                    last = next(
                        (i for i in range(len(lines) - 1, -1, -1) if lines[i].strip()),
                        None,
                    )
                    if last is not None and re.match(r"^\s*#\s*endif", lines[last]):
                        lines[last] = ""
        out.append(f"// --- {path.name} (from {path.parent.name}/) ---")
        for line in lines:
            m = LOCAL_INCLUDE.match(line)
            if m:
                resolved = resolve(m.group(1), path)
                if resolved is None:
                    sys.exit(f"amalgamate: cannot resolve {m.group(1)!r} from {path}")
                emit(resolved)
            else:
                out.append(line)

    emit(entry.resolve(), keep_guard=True)
    return "\n".join(out) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("entry", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("-I", dest="include_dirs", action="append", type=Path, default=[])
    arguments = parser.parse_args()
    arguments.output.write_text(
        amalgamate(arguments.entry, arguments.include_dirs), encoding="utf-8"
    )


if __name__ == "__main__":
    main()
