: inc ( value -- value ) 1+ ;

: benchmark ( -- result )
  0 10000000 0 do inc loop ;

: run-benchmark ( -- )
  cputime d+ 2>r
  benchmark
  cputime d+ 2r> d-
  2>r . cr 2r> d. cr ;

run-benchmark bye
