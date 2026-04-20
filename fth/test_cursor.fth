\ Test hiding and showing cursor
\ Hide cursor
"\033[?25l" print
"Cursor hidden. Sleeping for 2 seconds..." print
2000 sleep

\ Show cursor
"\033[?25h" print
"Cursor shown again." print
