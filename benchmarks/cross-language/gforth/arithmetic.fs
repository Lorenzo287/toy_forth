: benchmark ( -- result )
  0 10000000 0 do 1+ loop ;

: run-benchmark ( -- )
  cputime d+ 2>r
  benchmark
  cputime d+ 2r> d-
  2>r . cr 2r> d. cr ;

run-benchmark bye
