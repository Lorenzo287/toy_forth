\ Test loop primitives with escaped newlines

"Testing times (should print 3 times on one line):" print
3 [ " hello" printf ] times .

"Testing each (should print 1 2 3):" print
[ 1 2 3 ] [ " " printf printf ] each .

"Testing nested (should print 2x2 grid):" print
2 [ 
    2 [ "X " printf ] times .
    "---\n" printf
] times
