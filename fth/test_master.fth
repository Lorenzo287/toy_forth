"Math: 1 + 2 = 3\n" print
1 2 + println

"Math: 10 - 4 = 6\n" print
10 4 - println

"Math: 3 * 4 = 12\n" print
3 4 * println

"Math: 10 / 2 = 5\n" print
10 2 / println

"Math: Floats 1.5 + 2.0 = 3.5\n" print
1.5 2.0 + println

"Stack: dup (5 -> 5 5)" println
5 dup + println

"Stack: swap (1 2 -> 2 1)" println
1 2 swap - println

"Comp: 1 < 2 is true" println
1 2 < println

"Comp: 10 == 10 is true" println
10 10 == println

"Control: ifelse (true)" println
[ true ] [ "ok" ] [ "fail" ] ifelse println

"Control: while (countdown 3 to 1)" println
3 [ dup 0 > ] [ dup println 1 - ] while drop

"Define: colon (square 4 = 16)" println
: square dup * ;
4 square println

"Define: functional (cube 3 = 27)" println
'cube [ dup square * ] def
3 cube println
