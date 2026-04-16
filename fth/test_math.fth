\ Test new math functions

\ abs
-5 abs 5 == [ "abs int ok" println ] [ "abs int fail" println ] ifelse
-5.5 abs 5.5 == [ "abs float ok" println ] [ "abs float fail" println ] ifelse
0 abs 0 == [ "abs zero ok" println ] [ "abs zero fail" println ] ifelse

\ max
10 20 max 20 == [ "max int ok" println ] [ "max int fail" println ] ifelse
20 10 max 20 == [ "max int reversed ok" println ] [ "max int reversed fail" println ] ifelse
10.5 20.2 max 20.2 == [ "max float ok" println ] [ "max float fail" println ] ifelse
10 20.5 max 20.5 == [ "max mixed ok" println ] [ "max mixed fail" println ] ifelse

\ min
10 20 min 10 == [ "min int ok" println ] [ "min int fail" println ] ifelse
20 10 min 10 == [ "min int reversed ok" println ] [ "min int reversed fail" println ] ifelse
10.5 20.2 min 10.5 == [ "min float ok" println ] [ "min float fail" println ] ifelse
10 20.5 min 10.0 == [ "min mixed ok" println ] [ "min mixed fail" println ] ifelse

\ mod / %
10 3 % 1 == [ "% ok" println ] [ "% fail" println ] ifelse
10 3 mod 1 == [ "mod ok" println ] [ "mod fail" println ] ifelse
