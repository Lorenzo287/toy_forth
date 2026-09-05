# Log Report

A small, standalone text-processing application: parse request logs, aggregate
statistics by route, and render a sorted TSV report. It requires only Toy.

From the SDK or repository root, run the built-in sample or supply a file:

```console
toy examples/log-report/app
toy examples/log-report/app -- requests.tsv
```

The input has four **tab-separated** fields per record:

```text
/api/items?page=1	200	12	1000
/api/items?page=2	503	48	80
/health	200	2	16
```

Fields are route, HTTP status, integer latency in milliseconds, and integer
response bytes. Surrounding whitespace is trimmed; blank lines and lines
starting with `#` are ignored. LF and CRLF line endings are accepted, including
a final record without a newline. Routes must start with `/`. Query strings
are removed, but path case is preserved. Status must be in 100..599; latency
and byte counts must be non-negative. Malformed records stop the application
with an error. This is a simple TSV format, not an Apache/Nginx log parser or
a general URL normalizer.

The report contains `route`, `requests`, `errors`, `mean_ms`, `max_ms`, and
`bytes`, sorted by case-sensitive route order. Statuses 400..599 count as
errors; mean latency uses floating-point division, not integer truncation.

## Package

Import the `logs` directory relative to your Toy source file. Its public words
are:

| Word | Stack effect | Result |
| --- | --- | --- |
| `logs.analyze` | `text -- groups` | Route map with `[requests errors total-ms max-ms bytes]` values |
| `logs.rows` | `groups -- rows` | Sorted `[route requests errors total-ms max-ms bytes]` records |
| `logs.report` | `groups -- text` | TSV report with a header and final newline |

`rows` and `report` expect the shape produced by `analyze`. Input size and
aggregate totals must fit Toy's integer and memory limits.

The application reads the whole file. Analysis is expected linear work in
input bytes, with storage for split lines and distinct routes. Reporting
sorts `k` routes in O(k log k) comparisons and builds output from fragments
joined once. This is not a bounded-memory streaming implementation.

The growing map is threaded through `fold`; captures name stable input fields
and the fixed-size previous group. The distinction prevents repeated copies
of the growing map without making the source depend on a custom runtime word.
