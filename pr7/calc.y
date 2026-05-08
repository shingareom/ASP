%{
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* symbol table for variables (a..z) */
int sym[26];
int yylex(void);
void yyerror(const char *s);
%}

%token NUMBER VARIABLE NEWLINE SQRT
%left '+' '-'
%left '*' '/' '%'
%nonassoc UMINUS

%%
program:
        program statement
        | /* empty */
        ;

statement:
        expr NEWLINE         { printf("%d\n", $1); }
        | VARIABLE '=' expr NEWLINE { sym[$1 - 'a'] = $3; printf("%d\n", $3); }
        | NEWLINE
        | error NEWLINE      { yyerrok; }
        ;

expr:
        NUMBER               { $$ = $1; }
        | VARIABLE           {
                                 int idx = $1 - 'a';
                                 if (sym[idx] == 0 && idx != 0) { /* 0 is allowed value */
                                     fprintf(stderr, "Error: variable '%c' not initialized\n", $1);
                                     $$ = 0;
                                 } else {
                                     $$ = sym[idx];
                                 }
                             }
        | expr '+' expr      { $$ = $1 + $3; }
        | expr '-' expr      { $$ = $1 - $3; }
        | expr '*' expr      { $$ = $1 * $3; }
        | expr '/' expr      {
                                 if ($3 == 0) {
                                     yyerror("division by zero");
                                     $$ = 0;
                                 } else {
                                     $$ = $1 / $3;
                                 }
                             }
        | expr '%' expr      {
                                 if ($3 == 0) {
                                     yyerror("modulo by zero");
                                     $$ = 0;
                                 } else {
                                     $$ = $1 % $3;
                                 }
                             }
        | '(' expr ')'       { $$ = $2; }
        | '-' expr %prec UMINUS { $$ = -$2; }
        | SQRT '(' expr ')'  { $$ = (int)sqrt($3); }
        ;

%%

void yyerror(const char *s) {
    fprintf(stderr, "Error: %s\n", s);
}

int main(void) {
    printf("Simple Calculator (type expressions, # for comment, Ctrl+D to exit)\n");
    yyparse();
    return 0;
}
