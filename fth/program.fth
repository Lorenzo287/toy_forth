\ Classic style definition
: square dup * ;
5 square println

\ Functional style definition
'cube [ dup square * ] def
3 cube println

\ Control flow (while loop)
5 [ dup 0 > ] [ dup println 1 - ] while
