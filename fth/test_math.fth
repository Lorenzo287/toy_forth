1 2 + 3 == [ "int ok" printf ] [ "int fail" printf ] ifelse "\n" printf
1.5 2.0 + 3.5 == [ "float ok" printf ] [ "float fail" printf ] ifelse "\n" printf

-5 abs 5 == [ "abs int ok" print ] [ "abs int fail" print ] ifelse
-5.5 abs 5.5 == [ "abs float ok" print ] [ "abs float fail" print ] ifelse
0 abs 0 == [ "abs zero ok" print ] [ "abs zero fail" print ] ifelse

10 20 max 20 == [ "max int ok" print ] [ "max int fail" print ] ifelse
20 10 max 20 == [ "max int reversed ok" print ] [ "max int reversed fail" print ] ifelse
10.5 20.2 max 20.2 == [ "max float ok" print ] [ "max float fail" print ] ifelse
10 20.5 max 20.5 == [ "max mixed ok" print ] [ "max mixed fail" print ] ifelse

10 20 min 10 == [ "min int ok" print ] [ "min int fail" print ] ifelse
20 10 min 10 == [ "min int reversed ok" print ] [ "min int reversed fail" print ] ifelse
10.5 20.2 min 10.5 == [ "min float ok" print ] [ "min float fail" print ] ifelse
10 20.5 min 10.0 == [ "min mixed ok" print ] [ "min mixed fail" print ] ifelse

10 3 % 1 == [ "% ok" print ] [ "% fail" print ] ifelse
10 3 mod 1 == [ "mod ok" print ] [ "mod fail" print ] ifelse
