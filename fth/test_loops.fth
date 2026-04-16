\ Test loop primitives with escaped newlines

"Testing times (should print 3 times on one line):" print
3 [ " hello" print ] times "\n" print

"Testing each (should print 1 2 3):" print
[ 1 2 3 ] [ " " print print ] each "\n".

"Testing nested (should print 2x2 grid):" println
2 [ 
    2 [ "X " print ] times "\n".
    "---\n" print
] times
