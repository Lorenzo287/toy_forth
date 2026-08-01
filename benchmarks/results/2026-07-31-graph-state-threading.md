# Experiment: Multi-state graph traversal

- Date: 2026-07-31
- Baseline: commit `3837d75` plus the graph-search working tree
- Candidate: baseline plus snapshot-free state-threading continuations
- OS / CPU: Windows 11 10.0.26200 / Intel Core i7-1065G7
- Compiler / version: GCC 16.1.0
- Build configuration: Release
- Command: five fresh direct executions of `benchmarks/graph-search.toy`
- Change under test: compare direct and `dip`-based BFS state layouts, then
  remove `dip`'s private error-cleanup snapshot after the comparison exposed an
  ownership cliff

Graph construction and correctness checks run outside the timed region. Values
are medians of five fresh processes.

The initial runtime produced:

| Workload | 1,000 nodes | 5,000 nodes | 10,000 nodes |
| --- | ---: | ---: | ---: |
| BFS, directly threaded state | 0.856 ms | 3.786 ms | 7.741 ms |
| BFS, state beneath `dip` | 2.067 ms | 59.409 ms | 161.842 ms |
| Dijkstra, weighted line | 1.486 ms | 7.254 ms | 16.756 ms |
| Dijkstra, weighted star | 1.527 ms | 8.766 ms | 17.247 ms |

At 10,000 nodes the `dip` formulation was about 20.9 times slower and grew
about 78 times for 10 times as many nodes. Each `dip` retained the exposed
stack for private error restoration. Updating the seen set or deque therefore
observed another reference and copied the current collection on every loop.

That behavior was not part of `dip`'s successful dataflow. An enclosing `try`
already owns the stack snapshot when an error is recoverable, while an
unhandled host error has non-transactional stack effects. Removing the private
snapshot produced:

| Workload | 1,000 nodes | 5,000 nodes | 10,000 nodes |
| --- | ---: | ---: | ---: |
| BFS, directly threaded state | 0.927 ms | 3.878 ms | 7.942 ms |
| BFS, state beneath `dip` | 0.835 ms | 4.715 ms | 9.229 ms |
| Dijkstra, weighted line | 1.727 ms | 7.784 ms | 15.370 ms |
| Dijkstra, weighted star | 1.707 ms | 8.351 ms | 16.272 ms |

Both BFS layouts are now linear. At 10,000 nodes the `dip` version is about
1.16 times slower, consistent with its extra combinator calls and shuffles
rather than repeated collection copies.

The package keeps the direct `order queue seen` layout because it makes the
next aggregate operation explicit. Its two non-obvious shuffles are factored
into private words with stack-effect comments, while captures name only stable
graph context and scalar observations. The `dip` layout remains in the
benchmark as an ownership regression.

Dijkstra scales similarly on a line, where its priority queue stays tiny, and
on a star, where the queue grows to O(n). Its duplicate-entry/stale-pop design
therefore remains a good fit without requiring decrease-key.

The experiment changed the conclusion: the right response was to fix the
runtime, not teach a source-level workaround. State order still matters for
clarity and constant overhead, but `dip` should not make threaded values shared
merely to reconstruct an unhandled error.
