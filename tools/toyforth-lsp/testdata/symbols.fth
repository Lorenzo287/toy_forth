\ top-level definitions
: square ( n -- n*n ) { n } $n $n * ;

\ definition comment
'cube [ { n } $n $n $n * * ] def

5 square .
3 cube .

: shadow-demo { outer } $outer [ { outer } $outer ] exec $outer ;

\ nested forms should not create top-level symbols
[ : hidden 1 ; ]
{ shadow }

5 cube print
