cd /home/claude && gcc -Wall -Wextra -o assembler8086_pr5 assembler8086_pr5.c 2>&1 | grep "error:" ; ./assembler8086_pr5 test_pr5.asm 2>&1 | grep -A25 "PROCEDURE TABLE"
