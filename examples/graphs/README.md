# Graph Algorithm Examples

These standalone examples implement graph-search algorithms in Toy. They also
provide useful design evidence for combinations of stacks, combinators,
captures, and specialized collections, but the programs are examples rather
than a canonical definition of idiomatic Toy.

## Graph Search

The `graph` package currently provides:

| Word | Input representation | Result |
| --- | --- | --- |
| `graph.bfs` | map from node to a sequence of neighboring nodes, then a start node | visit-order vector |
| `graph.dijkstra` | map from node to `[neighbor weight]` pairs, then a start node | shortest-distance map |

Run the demonstration from the SDK or repository root:

```console
toy examples/graphs/demo
```

Both algorithms exercise more than one evolving value. BFS threads a deque,
set, and result vector directly through `while` and `each`. Dijkstra similarly
threads a priority queue and map. Captures name the graph, current node,
distance, and edge because those values remain stable while their local body
runs; they do not hold the collections being updated.

This produces a useful division of responsibility:

- the stack carries changing algorithm state;
- captures name stable context and scalar observations;
- `while` expresses the worklist loop;
- `each` processes adjacency sequences while preserving the rest of the state;
- deque, set, map, and priority queue are selected for operations the algorithm
  actually performs.

The direct `order queue seen` layout makes the next aggregate operation
explicit, while two small private words give the remaining shuffles clear
stack effects. An alternative BFS state layout remains in
[`benchmarks/graph-search.toy`](../../benchmarks/graph-search.toy). The recorded
[graph-state experiment](../../benchmarks/results/2026-07-31-graph-state-threading.md)
describes the runtime ownership issue that comparison exposed; it is supporting
evidence rather than a prescription for arranging every graph algorithm.

Dijkstra permits duplicate queue entries and ignores stale entries when they
are popped. This standard formulation avoids requiring a decrease-key API and
keeps the priority queue interface small. Negative reachable edge weights are
rejected.

The implementations live in a source package so the demonstration and
automated package test execute the exact same words. See
[Writing Idiomatic Toy](../../docs/idioms.md) for broader guidance on the
source patterns demonstrated here.
