\ Classic style definition
: square dup * ;
5 square print

\ Functional style definition
'cube [ dup square * ] def
3 cube print

\ Control flow (while loop)
5 [ dup 0 > ] [ dup print 1 - ] while
