"Math: 1 + 2 = 3\n" printf
1 2 + .

"Math: 10 - 4 = 6\n" printf
10 4 - .

"Math: 3 * 4 = 12\n" printf
3 4 * print

"Math: 10 / 2 = 5\n" printf
10 2 / print

"Math: Floats 1.5 + 2.0 = 3.5\n" printf
1.5 2.0 + .

"Stack: dup (5 -> 5 5)" .
5 dup + .

"Stack: swap (1 2 -> 2 1)" .
1 2 swap - print

"Comp: 1 < 2 is true" .
1 2 < print

"Comp: 10 == 10 is true" .
10 10 == print

"Control: ifelse (true)" .
[ true ] [ "ok" ] [ "fail" ] ifelse .

"Control: while (countdown 3 to 1)" .
3 [ dup 0 > ] [ dup . 1 - ] while drop

"Define: colon (square 4 = 16)" .
: square dup * ;
4 square .

"Define: functional (cube 3 = 27)" .
'cube [ dup square * ] def
3 cube .

"Vars: capture and fetch (10 (x) $x $x + = 20)" .
10 (x) $x $x + .

"Vars: dynamic scoping (100 (y) [ $y ] exec = 100)" .
100 (y) [ $y . ] exec
