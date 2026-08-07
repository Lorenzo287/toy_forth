# Toy REPL

Running `toy` without a package or evaluation option starts an interactive
REPL. The REPL is a good
place to learn Toy because the language can document itself: inspect the word
catalog, ask for a word's docs, try a small expression, and immediately see the
stack.

## Interactive Discovery

Useful first commands:

```text
toy> help
toy> 'map doc print
toy> 'map see print
toy> "priority" search-words
toy> words
```

The catalog groups words by primary concept, not by implementation file. Shared
words such as `push-back` or `pairs` are listed under their main language idea
even when they support more than one representation.

Consecutive `\` line comments or a `/* ... */` block comment immediately above
a user definition become that word's documentation, including definitions
loaded from packages:

```toy
/* Double a number. */
'double [ 2 * ] def

'double doc print
```

`doc` shows this description, while `see` shows the executable definition.
A blank line or another expression separates a comment from the definition.

## Interactive Development Loop

Type `trace` to toggle the automatic stack display. Type `hints` to toggle
syntax-aware input hints.

## Continuing after execution

Use `-i` or `--interactive` to enter the REPL after a package, source file, or
`--eval` program finishes successfully. The REPL keeps the same context, so
definitions, imported packages, and stack values remain available:

```console
toy --file experiment.toy -i
toy --eval "'double [ 2 * ] def" --interactive
toy --interactive examples/graphs/app
```

The initial program still runs normally. With `--tdb`, debugging starts at its
beginning and remains enabled when the REPL opens. To preload a file without
stepping through it and debug only a later call, omit `--tdb`, then toggle `tdb`
inside the REPL.

The REPL also provides a `load` word for adding a source file to an existing
session:

```toy
"experiments/helpers.toy" load
```

`load` has stack effect `path --`. It evaluates the file in the root context,
including its top-level effects, and preserves its filename in diagnostics and
debugger locations. Relative paths are resolved from the process working
directory, as with `--file`. The word exists only in the REPL; standalone files
and packages continue to use imports for reusable code. Loading a file again
re-executes it in the current state rather than resetting the session.

Definitions persist for the whole REPL session.

The data stack also persists. By default, the REPL prints the stack after each
evaluation. Use `.s` or `.S` when you want to inspect the stack explicitly 
(might be useful when trace toggle is off), `empty` to clear it.

## Scope of Captures

Captures created with `| ... |` live only while the word or quotation that
created them is running. They do not become persistent REPL variables:

```text
toy> 5 |a| $a print
5
toy> 5 |a|
toy> $a print
runtime error: undefined variable '$a'
```

Use `def` for persistent names.

## Runtime Diagnostics

Unhandled runtime and program errors report the source location and live data
stack. Errors inside nested words or quotations also include the Toy call
chain:

```text
runtime error: '+' expected number at stack depth 0, found string
  at example.toy:2:15
  stack <2> 10 "oops"
  in inner at example.toy:2:15
  in outer at example.toy:6:5
  in <program> at example.toy:9:1
```

After reporting the failure, the REPL unwinds the unfinished call frames and
keeps the surviving data stack for the next input. It does not roll back the
failed input: completed stack effects remain, while values hidden only inside
unfinished words are released. Use `.s` to inspect that stack, `empty` to
discard it, or `try` when the program needs explicit stack restoration.

## tdb Instruction Debugger

Type `tdb` to toggle the terminal debugger for subsequent REPL input. The VM
pauses before each Toy instruction and opens a distinct `(tdb)` prompt:

```text
toy> 'inc [ 1 + ] def
toy> tdb
toy[tdb]> 3 inc
paused: <repl>:1:1 in <program> (pc 0/2, depth 1)
  => 3
(tdb) step
paused: <repl>:1:3 in <program> (pc 1/2, depth 1)
  => inc
(tdb) step
paused: <repl>:1:8 in inc (pc 0/2, depth 2)
  => 1
```

The three prompts make debugger state explicit:

- `toy>` is the normal REPL;
- `toy[tdb]>` means tdb is enabled and waiting for an evaluation;
- `(tdb)` means an evaluation is currently paused.

Debugger commands are:

- `Enter`, `s`, or `step`: step into the displayed instruction;
- `n` or `next`: step over the displayed instruction;
- `out`, `finish`, or `step-out`: run until the current frame returns;
- `c` or `continue`: run until a breakpoint or the input completes;
- `b` or `break` followed by a line or word: add a breakpoint;
- `breakpoints` or `info break`: list breakpoints;
- `d` or `delete` followed by an ID: delete one breakpoint;
- `clear`: delete all breakpoints;
- `p` or `stack`: display the data stack without advancing;
- `locals` with an optional frame number: display captures owned by that frame;
- `print $name`: display the dynamically visible capture named `$name`;
- `words`: list user-defined words;
- `see name`: display a user-word definition or identify a native word;
- `bt` or `backtrace`: display program and native-continuation frames;
- `off`: disable tdb and continue;
- `q` or `abort`: unwind the current input as an interruption;
- `h`, `help`, or `?`: show the command summary.

Line breakpoints apply to the source containing the current pause; word
breakpoints stop before the first instruction of a named user word. This makes
`break update` useful across REPL inputs and later redefinitions, while
`break 12` is mainly useful when debugging a file. Breakpoint IDs remain valid
until deleted or tdb is disabled.

Native words are single instructions. If a native combinator schedules Toy
code, stepping enters the scheduled quotation on the next pause. `next` skips
Toy frames entered by the current instruction, and `finish` stops in its
caller. `continue` also applies to quotations scheduled by the current input.
The debugger remains enabled for the next REPL input unless `off` or
`tdb` turns it off. Standalone REPL controls such as `tdb`, `trace`, `hints`,
and `help` are handled outside VM evaluation, so toggling tdb does not stop to
debug the toggle itself.

The pause line describes only the current frame. Its `depth` field tells you
how many VM frames are active. `bt` is most useful when depth is greater than
one: it shows the complete chain of user words, quotations, native
continuations, and the root program. Frame zero is the current frame, so
`locals` is shorthand for `locals 0`; `locals 1` inspects its caller. Captures
belong to the frame where their `| ... |` list executed, while `print $name`
searches outward through active frames just like Toy's `$name` instruction.
`words` and `see` inspect the current context, including definitions created by
earlier REPL inputs and qualified words from imported packages.

To debug a file or an evaluated source string directly:

```console
toy --tdb --file program.toy
toy --tdb --file examples/factorial.toy
toy --tdb --eval "1 2 + print"
```

The process exits when package, file, or `--eval` execution finishes. Running
`toy --tdb` without one starts the REPL with tdb already armed. In an
existing REPL, import a package and call one of its qualified words to step
through that package's source.

## Multiline Input

The REPL keeps reading until the current input is structurally complete.

This applies to:

- quotations/vectors: `[ ... ]`
- lists: `( ... )`
- maps/sets: `{ ... }` / `#{ ... }`
- capture lists: `| ... |`
- strings: `" ... "`
- block comments: `/* ... */`

## Editing, History, Completion

The REPL uses `linenoise` for:

- line editing;
- command history;
- tab completion for known words;
- syntax-aware hints (`hints`);
- automatic stack display (`trace`);
- categorized help (`help`).

Stack display uses unambiguous value forms. Deques and priority queues are
shown as `deque[...]` and `pqueue[...]` display forms so they read as single
values. `repr` and `.S` still use reconstructable source forms.

## Exiting and Interrupting

To exit:

- Unix-like systems / WSL: `Ctrl-D`
- Windows console: `Ctrl-Z`
- at the prompt: press `Ctrl-C` twice in a row
- from Toy code: `exit`

Use `Ctrl-C` once to interrupt a running program such as an infinite loop. The
REPL reports this as an interrupt rather than as a generic runtime error. When
no program is running, the first `Ctrl-C` clears the current input and prints a
reminder that pressing `Ctrl-C` again exits. Interrupting running code, like an
unhandled error or `exit`, unwinds without rolling back completed stack effects.
