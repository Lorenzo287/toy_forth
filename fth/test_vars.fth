\ Test basic capture and fetch
10 (x) $x $x + println  \ Should print 20

\ Test swap implementation
: my-swap ( a b ) $b $a ;
1 2 my-swap println println \ Should print 1 then 2

\ Test dynamic scoping
100 (outer)
[ $outer println ] exec \ Should print 100

\ Test shadowing
10 (shadow)
[ 20 (shadow) $shadow println ] exec \ Should print 20
$shadow println \ Should print 10

\ Test multiline comments
/* This is a 
   multiline comment */
"Comment test passed" println
