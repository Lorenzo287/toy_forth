\ Test loop primitives with escaped newlines

"Testing times (should print 3 times on one line):" printf
3 [ " hello" printf ] times "\n" printf

"Testing each (should print 1 2 3):" printf
[ 1 2 3 ] [ " " printf printf ] each "" print

"Testing nested (should print 2x2 grid):" print
2 [ 
    2 [ "X " printf ] times "".
    "---\n" printf
] times
