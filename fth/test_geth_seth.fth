\ Test geth and seth
[ 10 20 30 ] {myList}

\ Test geth
$myList 0 geth .  \ Should print 10
$myList 1 geth .  \ Should print 20
$myList 2 geth .  \ Should print 30

\ Test seth
$myList 1 99 seth
$myList 1 geth .  \ Should print 99

\ Verify full list
$myList print
