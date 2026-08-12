# Testing Toy

Toy keeps automated regressions under `tests/toy/` and user-facing programs
under `examples/`. C API regressions live under `tests/c/`, while
buildable hosts live under `examples/embedding/`.

## Run the Suite

After bootstrapping Nob as described in the [build guide](build.md), run its
isolated test harness directly:

```console
nob test
nob test --filter native_loader
```

The default suite covers Toy cases, directory packages, debug-protocol
transport, embedding/debugger C tests, real loadable packages, `core:ffi`, and
generated bindings. It
builds incrementally using the selected compiler and mode. Each Toy case runs
in a fresh process with a
timeout and an isolated working directory under the build tree. This prevents
definitions, stack values, environment changes, and temporary files from
leaking between tests.

`--filter` selects tests whose names contain the given text:

```console
nob test --filter native_loader
nob test --filter bindgen
nob test --filter package
```

## File Conventions

The flat `tests/toy/` directory uses filename prefixes to declare how each case
is evaluated:

- `test_*.toy` must exit successfully. These files should use the test-only
  assertions from `testlib.toy` and remain silent on success where practical.
- `fail_*.toy` must exit with status 1. An adjacent `.stderr` file contains a
  stable fragment that must appear in the diagnostic.
- `output_*.toy` must exit successfully and match an adjacent `.stdout` file
  exactly after line-ending normalization.
- `manual_*.toy` covers interactive or visual behavior and is not registered
  with the automated suite.
- `testlib.toy` defines shared assertions and is copied beside each case in its
  isolated working directory.

Directory-package integration fixtures live under `tests/packages/` and cover
multi-file definitions, aliases, core imports, visibility, cycles, declaration
rules, and executable-package validation.

For example:

```text
tests/toy/fail_runtime_divide_by_zero.toy
tests/toy/fail_runtime_divide_by_zero.stderr
tests/toy/output_repr.toy
tests/toy/output_repr.stdout
```

Prefer value assertions for language semantics and stack effects, expected
failure cases for diagnostics, and golden output only when exact output is the
public contract.

## Manual and Integration Behavior

Terminal input, cursor control, ANSI behavior, and intentionally slow examples
belong in `manual_*.toy`. Filesystem, environment, and process regressions may
remain automated when they are deterministic and portable; the isolated test
working directory is available for their temporary files.

Do not combine cases by evaluating every test in one VM. Tests may define the
same words, and a shared stack or dictionary would make results order-dependent.
