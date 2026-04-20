[ 10 20 < ] [ "10 is less than 20\n" printf ] if
[ 30 20 < ] [ "30 is less than 20\n" printf ] [ "30 is not less than 20\n" printf ] ifelse

5 [ [ 0 > ] [ "positive" print ] [ "non-positive" print ] ifelse ] exec
5 neg [ [ 0 > ] [ "positive" print ] [ "non-positive" print ] ifelse ] exec

[ 1 2 + ] exec print
