: fib ( n -- fib-n )
  dup 2 < if exit then
  1- dup 1- recurse swap recurse + ;

: run-benchmark ( -- )
  cputime d+ 2>r
  32 fib
  cputime d+ 2r> d-
  2>r . cr 2r> d. cr ;

run-benchmark bye
