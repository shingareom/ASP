# Makefile for Lex/Yacc calculator (works with bison/flex)

CC = gcc
CFLAGS = -Wall -Wextra -g

# Use bison (or yacc) with -d to create header
YACC = bison -d
LEX = flex

# Output file names (bison uses prefix .tab, yacc uses y.tab)
# We force bison to use y.tab naming by adding -b y
# Alternatively, we detect and use the generated names.
# Simpler: use generic names and rename if needed.

all: calc

# Force bison to produce y.tab.c and y.tab.h
y.tab.c y.tab.h: calc.y
	$(YACC) -b y calc.y

lex.yy.c: calc.l y.tab.h
	$(LEX) calc.l

y.tab.o: y.tab.c
	$(CC) $(CFLAGS) -c y.tab.c

lex.yy.o: lex.yy.c
	$(CC) $(CFLAGS) -c lex.yy.c

calc: y.tab.o lex.yy.o
	$(CC) $(CFLAGS) -o calc y.tab.o lex.yy.o -lm

clean:
	rm -f calc y.tab.c y.tab.h lex.yy.c *.o

.PHONY: all clean
