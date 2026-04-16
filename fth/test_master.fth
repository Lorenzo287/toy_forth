"Math: 1 + 2 = 3" print
1 2 + print

"Math: 10 - 4 = 6" print
10 4 - print

"Math: 3 * 4 = 12" print
3 4 * print

"Math: 10 / 2 = 5" print
10 2 / print

"Math: Floats 1.5 + 2.0 = 3.5" print
1.5 2.0 + print

"Stack: dup (5 -> 5 5)" print
5 dup + print

"Stack: swap (1 2 -> 2 1)" print
1 2 swap - print

"Comp: 1 < 2 is true" print
1 2 < print

"Comp: 10 == 10 is true" print
10 10 == print

"Control: ifelse (true)" print
[ true ] [ "ok" ] [ "fail" ] ifelse print

"Control: while (countdown 3 to 1)" print
3 [ dup 0 > ] [ dup print 1 - ] while drop

"Define: colon (square 4 = 16)" print
: square dup * ;
4 square print

"Define: functional (cube 3 = 27)" print
'cube [ dup square * ] def
3 cube print
