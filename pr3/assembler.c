/*
 * ============================================================
 *  ASS3 – Hypothetical Machine Assembler (Single-Pass)
 * ============================================================
 *
 *  Hypothetical Machine specification
 *  -----------------------------------
 *  Word size   : 16 bits
 *  Address bus : 12 bits  (0x000 – 0xFFF, 4096 words)
 *  Format      : [OPCODE(4)] [OPERAND(12)]
 *
 *  Instruction set
 *  ---------------
 *  Mnemonic  Opcode  Description
 *  --------  ------  ------------------------------------
 *  ADD  addr   0x1   AC  ← AC + MEM[addr]
 *  SUB  addr   0x2   AC  ← AC - MEM[addr]
 *  MUL  addr   0x3   AC  ← AC * MEM[addr]
 *  DIV  addr   0x4   AC  ← AC / MEM[addr]
 *  LOAD addr   0x5   AC  ← MEM[addr]
 *  STOR addr   0x6   MEM[addr] ← AC
 *  JUMP addr   0x7   PC  ← addr
 *  JNEG addr   0x8   if AC < 0  then PC ← addr
 *  JZERO addr  0x9   if AC == 0 then PC ← addr
 *  HALT        0xA   Stop execution
 *  NOP         0xB   No operation
 *
 *  Pseudo-instructions (assembler directives)
 *  -------------------------------------------
 *  ORG  value   Set location counter to value
 *  DC   value   Define constant (stores a 16-bit literal)
 *  DS   count   Define storage (reserves <count> words, filled 0)
 *  END          Mark end of source
 *
 *  Source format (free-field, case-insensitive)
 *  --------------------------------------------
 *  [LABEL:]  MNEMONIC  [OPERAND]  [; comment]
 *
 *  Single-pass forward-reference resolution
 *  -----------------------------------------
 *  When a label is used before it is defined, the assembler stores
 *  the address of the instruction that needs patching in a
 *  "backpatch" list.  When the label is finally defined its value
 *  is written back into every entry on the list.
 *
 *  Output
 *  ------
 *  1. Symbol table  (label → address)
 *  2. Object listing (address | hex object code | source line)
 *  3. Binary-image file  assembler.obj  (one 16-bit word per line, hex)
 *
 *  Usage
 *  -----
 *    ./assembler  input.asm
 *
 *  Build
 *  -----
 *    gcc -Wall -Wextra -o assembler assembler.c
 * ============================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

/* ------------------------------------------------------------------ */
/*  Constants                                                           */
/* ------------------------------------------------------------------ */
#define MAX_MEMORY     4096      /* 12-bit address space               */
#define MAX_LABELS     512       /* symbol table capacity              */
#define MAX_BACKPATCH  1024      /* forward-reference list capacity    */
#define MAX_LINE       256       /* max source-line length             */
#define MAX_TOKEN      64        /* max token length                   */
#define UNDEF          -1        /* sentinel: label not yet defined    */

/* ------------------------------------------------------------------ */
/*  Instruction table entry                                             */
/* ------------------------------------------------------------------ */
typedef struct {
    char    mnemonic[MAX_TOKEN];
    int     opcode;        /* -1 = pseudo-instruction (no object code) */
    int     has_operand;   /* 1 = takes an address / literal           */
} InstrEntry;

/* Instruction set + pseudo-ops                                         */
static const InstrEntry INSTR_TABLE[] = {
    /* real instructions */
    {"ADD",   0x1, 1},
    {"SUB",   0x2, 1},
    {"MUL",   0x3, 1},
    {"DIV",   0x4, 1},
    {"LOAD",  0x5, 1},
    {"STOR",  0x6, 1},
    {"JUMP",  0x7, 1},
    {"JNEG",  0x8, 1},
    {"JZERO", 0x9, 1},
    {"HALT",  0xA, 0},
    {"NOP",   0xB, 0},
    /* pseudo-instructions (opcode -1 = handled specially) */
    {"ORG",  -1,  1},
    {"DC",   -1,  1},
    {"DS",   -1,  1},
    {"END",  -1,  0},
    {"",      0,  0}   /* sentinel */
};

/* ------------------------------------------------------------------ */
/*  Symbol table                                                        */
/* ------------------------------------------------------------------ */
typedef struct {
    char label[MAX_TOKEN];
    int  address;     /* UNDEF while forward-referenced */
} Symbol;

static Symbol sym_table[MAX_LABELS];
static int    sym_count = 0;

/* ------------------------------------------------------------------ */
/*  Backpatch list                                                      */
/* ------------------------------------------------------------------ */
typedef struct {
    int  patch_addr;   /* memory address whose operand field needs fixing */
    char label[MAX_TOKEN];
} BackpatchEntry;

static BackpatchEntry backpatch[MAX_BACKPATCH];
static int            bp_count = 0;

/* ------------------------------------------------------------------ */
/*  Object memory                                                       */
/* ------------------------------------------------------------------ */
static int memory[MAX_MEMORY];   /* assembled words                    */
static int mem_used[MAX_MEMORY]; /* 1 = word has been written          */

/* ------------------------------------------------------------------ */
/*  Error counter                                                       */
/* ------------------------------------------------------------------ */
static int error_count = 0;

/* ================================================================== */
/*  Helper utilities                                                    */
/* ================================================================== */

/* Convert string to upper-case in-place */
static void str_toupper(char *s)
{
    for (; *s; s++) *s = (char)toupper((unsigned char)*s);
}

/* Strip leading/trailing whitespace; return pointer to first non-space */
static char *trim(char *s)
{
    while (isspace((unsigned char)*s)) s++;
    char *end = s + strlen(s) - 1;
    while (end >= s && isspace((unsigned char)*end)) *end-- = '\0';
    return s;
}

/* Print an assembly error */
static void asm_error(int line_no, const char *msg)
{
    fprintf(stderr, "  [ERROR] Line %d: %s\n", line_no, msg);
    error_count++;
}

/* ================================================================== */
/*  Symbol-table operations                                             */
/* ================================================================== */

/* Find symbol; returns index or -1 */
static int sym_find(const char *label)
{
    for (int i = 0; i < sym_count; i++)
        if (strcmp(sym_table[i].label, label) == 0) return i;
    return -1;
}

/* Add a new symbol (UNDEF) or update existing; returns index */
static int sym_add(const char *label, int address)
{
    int idx = sym_find(label);
    if (idx == -1) {
        if (sym_count >= MAX_LABELS) { fputs("Symbol table overflow\n", stderr); exit(1); }
        strncpy(sym_table[sym_count].label, label, MAX_TOKEN - 1);
        sym_table[sym_count].address = address;
        return sym_count++;
    }
    /* Update if it was forward-referenced */
    if (sym_table[idx].address == UNDEF) sym_table[idx].address = address;
    return idx;
}

/* ================================================================== */
/*  Backpatch operations                                                */
/* ================================================================== */

/* Schedule a forward-reference fix */
static void bp_add(int patch_addr, const char *label)
{
    if (bp_count >= MAX_BACKPATCH) { fputs("Backpatch list overflow\n", stderr); exit(1); }
    backpatch[bp_count].patch_addr = patch_addr;
    strncpy(backpatch[bp_count].label, label, MAX_TOKEN - 1);
    bp_count++;
}

/* Apply all pending backpatches for a newly-defined label */
static void bp_resolve(const char *label, int address, int line_no)
{
    for (int i = 0; i < bp_count; i++) {
        if (strcmp(backpatch[i].label, label) == 0) {
            int pa = backpatch[i].patch_addr;
            /* The opcode is already in the high nibble; patch the low 12 bits */
            memory[pa] = (memory[pa] & 0xF000) | (address & 0x0FFF);
            backpatch[i].patch_addr = -1;   /* mark as resolved */
            (void)line_no;
        }
    }
}

/* ================================================================== */
/*  Instruction-table lookup                                            */
/* ================================================================== */
static const InstrEntry *find_instr(const char *mnemonic)
{
    for (int i = 0; INSTR_TABLE[i].mnemonic[0]; i++)
        if (strcmp(INSTR_TABLE[i].mnemonic, mnemonic) == 0)
            return &INSTR_TABLE[i];
    return NULL;
}

/* ================================================================== */
/*  Parse an operand: either a decimal/hex literal or a label          */
/*  Returns the numeric value, and sets *is_label if it is a label.    */
/* ================================================================== */
static int parse_operand(const char *tok, int *is_label)
{
    *is_label = 0;
    if (!tok || tok[0] == '\0') return 0;

    /* Hex literal: 0x... */
    if (tok[0] == '0' && (tok[1] == 'x' || tok[1] == 'X'))
        return (int)strtol(tok, NULL, 16);

    /* Decimal literal */
    if (isdigit((unsigned char)tok[0]) ||
        (tok[0] == '-' && isdigit((unsigned char)tok[1])))
        return (int)strtol(tok, NULL, 10);

    /* Label */
    *is_label = 1;
    return 0;
}

/* ================================================================== */
/*  Single-pass assembly                                                */
/* ================================================================== */

/*
 *  Listing output buffer — we collect lines so we can print the
 *  symbol table first, then the full listing.
 */
#define MAX_LISTING_LINES 2048
typedef struct { char text[MAX_LINE * 2]; } ListLine;
static ListLine listing[MAX_LISTING_LINES];
static int      listing_count = 0;

static void list_append(const char *fmt, ...)
{
    if (listing_count >= MAX_LISTING_LINES) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(listing[listing_count++].text, sizeof(listing[0].text), fmt, ap);
    va_end(ap);
}

static void assemble(FILE *src)
{
    int  lc        = 0;   /* Location Counter                         */
    int  line_no   = 0;
    char line[MAX_LINE];

    list_append("%-6s  %-6s  %-6s  %s\n", "Addr", "ObjCode", "LC", "Source");
    list_append("%s\n", "------  ------  ------  --------------------------------");

    while (fgets(line, sizeof(line), src)) {
        line_no++;

        /* ---- strip newline and comments ---- */
        char *semi = strchr(line, ';');
        if (semi) *semi = '\0';
        char *p = trim(line);
        if (*p == '\0') {
            list_append("                        %s\n", "(blank)");
            continue;
        }

        char orig_line[MAX_LINE];
        strncpy(orig_line, p, MAX_LINE - 1);

        /* ---- extract label (optional, ends with ':') ---- */
        char label_tok[MAX_TOKEN] = "";
        char *colon = strchr(p, ':');
        if (colon) {
            int llen = (int)(colon - p);
            if (llen >= MAX_TOKEN) { asm_error(line_no, "Label too long"); llen = MAX_TOKEN - 1; }
            strncpy(label_tok, p, llen);
            label_tok[llen] = '\0';
            str_toupper(label_tok);
            trim(label_tok);
            p = trim(colon + 1);
        }

        /* ---- extract mnemonic ---- */
        char mnemonic[MAX_TOKEN] = "";
        char operand_tok[MAX_TOKEN] = "";
        {
            char buf[MAX_LINE];
            strncpy(buf, p, MAX_LINE - 1);
            char *tok1 = strtok(buf, " \t,");
            if (tok1) {
                strncpy(mnemonic, tok1, MAX_TOKEN - 1);
                str_toupper(mnemonic);
            }
            char *tok2 = strtok(NULL, " \t,");
            if (tok2) {
                strncpy(operand_tok, tok2, MAX_TOKEN - 1);
                str_toupper(operand_tok);
            }
        }

        if (mnemonic[0] == '\0') {
            /* Line has only a label */
            if (label_tok[0]) {
                sym_add(label_tok, lc);
                bp_resolve(label_tok, lc, line_no);
            }
            list_append("%04X              %04X    %s\n", lc, lc, orig_line);
            continue;
        }

        /* ---- look up mnemonic ---- */
        const InstrEntry *ie = find_instr(mnemonic);
        if (!ie) {
            asm_error(line_no, "Unknown mnemonic");
            list_append("????  ????    %04X    %s\n", lc, orig_line);
            continue;
        }

        /* ---- define label (if present) at current LC ---- */
        if (label_tok[0]) {
            int existing = sym_find(label_tok);
            if (existing != -1 && sym_table[existing].address != UNDEF) {
                char msg[128];
                snprintf(msg, sizeof(msg), "Duplicate label '%s'", label_tok);
                asm_error(line_no, msg);
            } else {
                sym_add(label_tok, lc);
                bp_resolve(label_tok, lc, line_no);
            }
        }

        /* ================================================================
         *  Handle pseudo-instructions
         * ================================================================ */
        if (ie->opcode == -1) {

            if (strcmp(mnemonic, "ORG") == 0) {
                int is_lbl;
                int val = parse_operand(operand_tok, &is_lbl);
                if (is_lbl) { asm_error(line_no, "ORG requires a numeric operand"); }
                else        { lc = val & 0x0FFF; }
                list_append("          (%04X)  %04X    %s\n", val, lc, orig_line);

            } else if (strcmp(mnemonic, "DC") == 0) {
                int is_lbl;
                int val = parse_operand(operand_tok, &is_lbl);
                if (is_lbl) { asm_error(line_no, "DC requires a numeric literal"); }
                else {
                    if (lc >= MAX_MEMORY) { asm_error(line_no, "Address out of range"); }
                    else {
                        memory[lc] = val & 0xFFFF;
                        mem_used[lc] = 1;
                        list_append("%04X    %04X    %04X    %s\n", lc, memory[lc], lc, orig_line);
                        lc++;
                    }
                }

            } else if (strcmp(mnemonic, "DS") == 0) {
                int is_lbl;
                int count = parse_operand(operand_tok, &is_lbl);
                if (is_lbl || count <= 0) { asm_error(line_no, "DS requires a positive integer"); }
                else {
                    list_append("%04X    (res%d)  %04X    %s\n", lc, count, lc, orig_line);
                    for (int k = 0; k < count && lc < MAX_MEMORY; k++, lc++) {
                        memory[lc]  = 0;
                        mem_used[lc] = 1;
                    }
                }

            } else if (strcmp(mnemonic, "END") == 0) {
                list_append("                        %s\n", orig_line);
                break;   /* stop assembling */
            }
            continue;
        }

        /* ================================================================
         *  Handle real instructions
         * ================================================================ */
        if (lc >= MAX_MEMORY) { asm_error(line_no, "Address out of range"); continue; }

        int obj_word = (ie->opcode & 0xF) << 12;

        if (ie->has_operand) {
            int is_lbl;
            int val = parse_operand(operand_tok, &is_lbl);

            if (is_lbl) {
                /* Look up symbol */
                int idx = sym_find(operand_tok);
                if (idx != -1 && sym_table[idx].address != UNDEF) {
                    /* Already defined – use it directly */
                    val = sym_table[idx].address;
                    obj_word |= (val & 0x0FFF);
                } else {
                    /* Forward reference – add placeholder, schedule backpatch */
                    if (idx == -1) sym_add(operand_tok, UNDEF);
                    bp_add(lc, operand_tok);
                    obj_word |= 0x000;   /* will be patched later */
                }
            } else {
                obj_word |= (val & 0x0FFF);
            }
        }

        memory[lc]  = obj_word;
        mem_used[lc] = 1;
        list_append("%04X    %04X    %04X    %s\n", lc, memory[lc], lc, orig_line);
        lc++;
    }

    /* ================================================================
     *  Check for unresolved forward references
     * ================================================================ */
    for (int i = 0; i < bp_count; i++) {
        if (backpatch[i].patch_addr == -1) continue;  /* already resolved */
        int idx = sym_find(backpatch[i].label);
        if (idx == -1 || sym_table[idx].address == UNDEF) {
            char msg[128];
            snprintf(msg, sizeof(msg),
                     "Unresolved label '%s' at address %04X",
                     backpatch[i].label, backpatch[i].patch_addr);
            fprintf(stderr, "  [ERROR] %s\n", msg);
            error_count++;
        }
    }
}

/* ================================================================== */
/*  Write object file (hex words, one per line)                        */
/* ================================================================== */
static void write_object(const char *filename)
{
    FILE *f = fopen(filename, "w");
    if (!f) { perror(filename); return; }
    fprintf(f, "; Hypothetical Machine Object File\n");
    fprintf(f, "; Format: ADDRESS WORD\n");
    for (int i = 0; i < MAX_MEMORY; i++) {
        if (mem_used[i]) fprintf(f, "%04X %04X\n", i, memory[i] & 0xFFFF);
    }
    fclose(f);
}

/* ================================================================== */
/*  main                                                                */
/* ================================================================== */
int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <source.asm>\n", argv[0]);
        return 1;
    }

    FILE *src = fopen(argv[1], "r");
    if (!src) { perror(argv[1]); return 1; }

    /* Initialise memory */
    memset(memory,   0, sizeof(memory));
    memset(mem_used, 0, sizeof(mem_used));

    printf("============================================================\n");
    printf("  Hypothetical Machine Assembler  (Single-Pass)\n");
    printf("  Source file : %s\n", argv[1]);
    printf("============================================================\n\n");

    /* ---- Single pass ---- */
    assemble(src);
    fclose(src);

    /* ---- Symbol table ---- */
    printf("SYMBOL TABLE\n");
    printf("%-16s  %s\n", "Label", "Address");
    printf("%-16s  %s\n", "----------------", "-------");
    for (int i = 0; i < sym_count; i++) {
        if (sym_table[i].address == UNDEF)
            printf("%-16s  UNDEF\n", sym_table[i].label);
        else
            printf("%-16s  %04X\n", sym_table[i].label, sym_table[i].address);
    }
    if (sym_count == 0) printf("  (empty)\n");

    /* ---- Listing ---- */
    printf("\nOBJECT LISTING\n");
    for (int i = 0; i < listing_count; i++) printf("%s", listing[i].text);

    /* ---- Summary ---- */
    printf("\n------------------------------------------------------------\n");
    if (error_count == 0) {
        printf("Assembly completed successfully. No errors.\n");
        write_object("assembler.obj");
        printf("Object file written to: assembler.obj\n");
    } else {
        printf("Assembly finished with %d error(s). No object file written.\n",
               error_count);
    }
    printf("============================================================\n");

    return error_count ? 1 : 0;
}
