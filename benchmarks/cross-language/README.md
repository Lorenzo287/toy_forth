# Cross-language benchmarks

This optional matrix compares common interpreted-language workloads without
turning them into a single score. It reports both fresh-process wall time and
CPU time measured around the workload itself. Every implementation prints a
checksum, and `run.py` rejects a missing or incorrect result.

The workloads are deliberately small and inspectable:

- `arithmetic`: ten million inline integer increments;
- `dispatch`: ten million calls to a user-defined increment function or word;
- `fibonacci`: idiomatic recursive Fibonacci(32), using `binrec` in the
  concatenative languages;
- `sequence`: ten builds and interpreted sums of 20,000 integer elements;
- `string`: twenty incremental builds of 10,000-byte strings, using the
  idiomatic builder available in each language;
- `map-lookup`: 50,000 integer entries followed by one million lookups; Joy is
  omitted because it has no comparable associative collection;
- `startup`: a fresh process reading an empty source file.

Sequence and string rows compare idiomatic language-level operations, not
identical storage structures. JavaScript JIT compilation is included in the
in-program time. Gforth appears only for startup and the scalar/control
workloads: its unboxed cells and threaded execution make it a useful stack
dispatch reference, not a comparable high-level collection runtime. Startup
and file-loading costs appear only in the process table.

## Environment

The runner executes under Linux so Joy0's `sizeof(long) == sizeof(void *)`
assumption remains valid. It expects the two Joy repositories as siblings of
Toy under `../concat/`, with Release executables at `build-linux/joy`.
It also expects these ignored build artifacts:

- a clean Linux Toy build at
  `build/cross-language/toy-linux/build/gcc/release/toy`;
- Lua 5.5.0 at `build/cross-language/tools/lua-5.5.0/src/lua`;
- Janet 1.41.2 at
  `build/cross-language/tools/janet-1.41.2/build/janet`;
- Bun at `build/cross-language/tools/bun-linux-x64/bun`.

Lua should be built from the official release source with its default Linux
target. Janet should be built from the official `v1.41.2` tag using its default
`make` target. Bun should be the official Linux x64 release and invoked through
`bun run`. Python, Node, and Gforth are read from the Linux environment; the
runner currently expects Gforth at `/usr/bin/gforth`.

Before measuring, the runner copies executables stored below WSL's `/mnt/`
mount into a temporary native-Linux directory. This prevents large binaries
from inheriting Windows-filesystem paging costs in the process-time table. The
temporary copies are removed when the run completes.

Run the complete matrix from WSL:

```console
python3 benchmarks/cross-language/run.py --runs 7
```

Select individual workloads while developing them:

```console
python3 benchmarks/cross-language/run.py --runs 3 arithmetic dispatch
```

The cross-language dependencies remain outside the normal Nob build because
they are optional, comparatively large, and specific to performance research.
Record runtime versions, compiler options, hardware, and operating system with
any durable result.
