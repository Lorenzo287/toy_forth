10 {x} $x $x + print  \ Should print 20

'my-swap [ { a b } $b $a ] def

1 2 my-swap print print \ Should print 1 then 2

100 {outer}
[ $outer print ] exec \ Should print 100

10 {shadow}
[ 20 {shadow} $shadow print ] exec \ Should print 20
$shadow print \ Should print 10

\ Comments support
\ This is a comment
( This is a
   block comment )
"Comment test passed" print
