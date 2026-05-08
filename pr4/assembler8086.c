/*
 * ============================================================
 *  PR4 – 8086 Two-Pass Assembler
 * ============================================================
 *
 *  Features
 *  --------
 *  • Two-pass assembly (Pass 1: build symbol table + assign addresses
 *                        Pass 2: generate object code)
 *  • Segment support  : .DATA  and  .CODE  (+ .STACK)
 *  • Symbol Table     : label → segment + offset
 *  • Error Table      : collects all errors with line numbers
 *  • Supported instructions (common 8086 subset):
 *      MOV, ADD, SUB, MUL, DIV, INC, DEC
 *      AND, OR, XOR, NOT, SHL, SHR
 *      CMP, JMP, JE, JNE, JL, JG, JLE, JGE, JZ, JNZ
 *      PUSH, POP, CALL, RET, INT
 *      NOP, HLT
 *  • Directives: .MODEL, .STACK, .DATA, .CODE, ENDS, END
 *                DB, DW, DD, DUP, EQU, ORG
 *  • Register operands : AX BX CX DX SI DI SP BP
 *                        AH AL BH BL CH CL DH DL
 *                        CS DS SS ES
 *  • Addressing modes  : register, immediate, direct memory, register indirect
 *  • Output: Symbol Table, Error Table, Object Listing (.obj hex file)
 *
 *  Usage
 *  -----
 *    ./assembler8086 input.asm
 *
 *  Build
 *  -----
 *    gcc -Wall -Wextra -o assembler8086 assembler8086.c
 * ============================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

/* ------------------------------------------------------------------ */
/*  Limits & Sentinels                                                  */
/* ------------------------------------------------------------------ */
#define MAX_LINE         256
#define MAX_TOKEN        64
#define MAX_SYMBOLS      512
#define MAX_ERRORS       256
#define MAX_OBJ_BYTES    65536   /* 64 KB flat buffer                  */
#define MAX_SOURCE_LINES 4096
#define UNDEF_ADDR       -1

/* ------------------------------------------------------------------ */
/*  Segment IDs                                                         */
/* ------------------------------------------------------------------ */
typedef enum { SEG_NONE = 0, SEG_DATA, SEG_CODE, SEG_STACK } SegID;

static const char *seg_name(SegID s) {
    switch(s) { case SEG_DATA: return "DATA"; case SEG_CODE: return "CODE";
                case SEG_STACK: return "STACK"; default: return "NONE"; }
}

/* ------------------------------------------------------------------ */
/*  Symbol Table                                                        */
/* ------------------------------------------------------------------ */
typedef enum { SYM_LABEL, SYM_EQU, SYM_VAR } SymKind;

typedef struct {
    char   name[MAX_TOKEN];
    SegID  segment;
    int    offset;       /* offset within segment (or EQU value)   */
    int    size;         /* byte size: 1=DB,2=DW,4=DD,0=label      */
    SymKind kind;
} Symbol;

static Symbol   sym_table[MAX_SYMBOLS];
static int      sym_count = 0;

/* ------------------------------------------------------------------ */
/*  Error Table                                                         */
/* ------------------------------------------------------------------ */
typedef struct {
    int  line_no;
    char message[MAX_LINE];
} ErrorEntry;

static ErrorEntry err_table[MAX_ERRORS];
static int        err_count = 0;

/* ------------------------------------------------------------------ */
/*  Object byte buffer (flat, we track segment offsets separately)     */
/* ------------------------------------------------------------------ */
static unsigned char obj_data[MAX_OBJ_BYTES];
static int           obj_data_start = 0;   /* DATA segment base in obj  */
static int           obj_code_start = 0;   /* CODE segment base in obj  */
static int           obj_stack_size = 256; /* default stack size         */

/* per-segment location counters */
static int lc_data  = 0;
static int lc_code  = 0;
static int lc_stack = 0;

/* active segment during pass */
static SegID cur_seg = SEG_NONE;

/* ------------------------------------------------------------------ */
/*  Source line storage (for pass 2)                                    */
/* ------------------------------------------------------------------ */
typedef struct {
    char   text[MAX_LINE];
    int    line_no;
} SrcLine;

static SrcLine  src_lines[MAX_SOURCE_LINES];
static int      src_count  = 0;

/* ------------------------------------------------------------------ */
/*  Object listing lines                                                */
/* ------------------------------------------------------------------ */
typedef struct {
    int   seg_offset;      /* -1 = no code generated (directive/blank) */
    SegID seg;
    unsigned char bytes[16];
    int   byte_count;
    char  source[MAX_LINE];
    int   line_no;
} ListEntry;

static ListEntry list_table[MAX_SOURCE_LINES];
static int       list_count = 0;

/* ================================================================== */
/*  Utility                                                             */
/* ================================================================== */

static void str_toupper(char *s) {
    for (; *s; s++) *s = (char)toupper((unsigned char)*s);
}

static char *ltrim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    return s;
}

static char *trim(char *s) {
    s = ltrim(s);
    int len = (int)strlen(s);
    while (len > 0 && isspace((unsigned char)s[len-1])) s[--len] = '\0';
    return s;
}

/* Add an error to the error table */
static void add_error(int line_no, const char *fmt, ...) {
    if (err_count >= MAX_ERRORS) return;
    err_table[err_count].line_no = line_no;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err_table[err_count].message, MAX_LINE, fmt, ap);
    va_end(ap);
    err_count++;
}

/* ================================================================== */
/*  Register table                                                      */
/* ================================================================== */
typedef struct { const char *name; int code; int is8bit; int isseg; } RegEntry;

static const RegEntry reg_table[] = {
    /* 16-bit general */
    {"AX",0,0,0},{"BX",1,0,0},{"CX",2,0,0},{"DX",3,0,0},
    {"SP",4,0,0},{"BP",5,0,0},{"SI",6,0,0},{"DI",7,0,0},
    /* 8-bit */
    {"AL",0,1,0},{"CL",1,1,0},{"DL",2,1,0},{"BL",3,1,0},
    {"AH",4,1,0},{"CH",5,1,0},{"DH",6,1,0},{"BH",7,1,0},
    /* segment */
    {"ES",0,0,1},{"CS",1,0,1},{"SS",2,0,1},{"DS",3,0,1},
    {NULL,0,0,0}
};

static const RegEntry *find_reg(const char *name) {
    char up[MAX_TOKEN]; strncpy(up, name, MAX_TOKEN-1); str_toupper(up);
    for (int i = 0; reg_table[i].name; i++)
        if (strcmp(reg_table[i].name, up) == 0) return &reg_table[i];
    return NULL;
}

/* ================================================================== */
/*  Symbol Table Operations                                             */
/* ================================================================== */

static int sym_find(const char *name) {
    char up[MAX_TOKEN]; strncpy(up, name, MAX_TOKEN-1); str_toupper(up);
    for (int i = 0; i < sym_count; i++)
        if (strcmp(sym_table[i].name, up) == 0) return i;
    return -1;
}

/* Add symbol; returns index or -1 on overflow/duplicate */
static int sym_add(const char *name, SegID seg, int offset, int size, SymKind kind, int line_no) {
    char up[MAX_TOKEN]; strncpy(up, name, MAX_TOKEN-1); str_toupper(up);
    int existing = sym_find(up);
    if (existing != -1) {
        /* Already exists – only error if both passes defined it (dup) */
        /* During pass1 update is ok if UNDEF */
        if (sym_table[existing].offset != UNDEF_ADDR) {
            add_error(line_no, "Duplicate label '%s'", up);
            return existing;
        }
        sym_table[existing].segment = seg;
        sym_table[existing].offset  = offset;
        sym_table[existing].size    = size;
        sym_table[existing].kind    = kind;
        return existing;
    }
    if (sym_count >= MAX_SYMBOLS) { add_error(line_no, "Symbol table overflow"); return -1; }
    strncpy(sym_table[sym_count].name, up, MAX_TOKEN-1);
    sym_table[sym_count].segment = seg;
    sym_table[sym_count].offset  = offset;
    sym_table[sym_count].size    = size;
    sym_table[sym_count].kind    = kind;
    return sym_count++;
}

/* ================================================================== */
/*  Instruction Encoding Helpers                                        */
/* ================================================================== */

/*
 * Operand descriptor
 */
typedef enum {
    OT_NONE,
    OT_REG,      /* register */
    OT_IMM,      /* immediate value */
    OT_MEM,      /* direct memory [addr] or label */
    OT_REGIND    /* register indirect [BX], [SI], [DI], [BP] */
} OpType;

typedef struct {
    OpType type;
    int    reg_code;   /* for OT_REG / OT_REGIND */
    int    is8bit;
    int    isseg;
    int    value;      /* for OT_IMM or OT_MEM (address/symbol value) */
    char   sym[MAX_TOKEN]; /* symbolic name for OT_MEM if label */
    int    sym_unresolved;
} Operand;

/* Parse a single operand string into an Operand descriptor */
static Operand parse_operand(const char *tok, int line_no) {
    Operand op = {OT_NONE, 0, 0, 0, 0, {0}, 0};
    if (!tok || tok[0] == '\0') return op;

    char buf[MAX_TOKEN];
    strncpy(buf, tok, MAX_TOKEN-1);
    char *t = trim(buf);
    char up[MAX_TOKEN]; strncpy(up, t, MAX_TOKEN-1); str_toupper(up);

    /* Register? */
    const RegEntry *re = find_reg(up);
    if (re) {
        op.type     = OT_REG;
        op.reg_code = re->code;
        op.is8bit   = re->is8bit;
        op.isseg    = re->isseg;
        return op;
    }

    /* Memory / register-indirect: [xxx] */
    if (t[0] == '[') {
        char inner[MAX_TOKEN];
        int len = (int)strlen(t);
        if (t[len-1] == ']') {
            strncpy(inner, t+1, len-2); inner[len-2] = '\0';
            trim(inner);
            char iup[MAX_TOKEN]; strncpy(iup, inner, MAX_TOKEN-1); str_toupper(iup);
            re = find_reg(iup);
            if (re) {
                op.type = OT_REGIND;
                op.reg_code = re->code;
                return op;
            }
            /* numeric or label inside brackets = direct */
        }
        /* fall through to direct memory */
        strncpy(inner, t+1, (size_t)(len-2)); inner[len-2] = '\0';
        trim(inner);
        op.type = OT_MEM;
        char *end;
        long val = strtol(inner, &end, 0);
        if (*end == '\0') { op.value = (int)val; }
        else {
            /* symbolic */
            char sym_up[MAX_TOKEN]; strncpy(sym_up, inner, MAX_TOKEN-1); str_toupper(sym_up);
            int idx = sym_find(sym_up);
            if (idx == -1) {
                op.sym_unresolved = 1;
                strncpy(op.sym, sym_up, MAX_TOKEN-1);
                add_error(line_no, "Undefined symbol '%s'", sym_up);
            } else {
                op.value = sym_table[idx].offset;
                strncpy(op.sym, sym_up, MAX_TOKEN-1);
            }
        }
        return op;
    }

    /* Immediate: starts with digit, '-', or '0x' */
    if (isdigit((unsigned char)t[0]) || t[0] == '-' ||
        (t[0] == '0' && (t[1]=='x'||t[1]=='X'))) {
        op.type  = OT_IMM;
        op.value = (int)strtol(t, NULL, 0);
        return op;
    }

    /* Check for hex literal ending in 'H' / 'h' */
    {
        int len = (int)strlen(t);
        if (len > 1 && toupper((unsigned char)t[len-1]) == 'H') {
            char hbuf[MAX_TOKEN]; strncpy(hbuf, t, len-1); hbuf[len-1]='\0';
            char *end;
            long val = strtol(hbuf, &end, 16);
            if (*end == '\0') {
                op.type  = OT_IMM;
                op.value = (int)val;
                return op;
            }
        }
    }

    /* Label / symbol (bare, used as immediate address or jump target) */
    {
        char sym_up[MAX_TOKEN]; strncpy(sym_up, up, MAX_TOKEN-1);
        int idx = sym_find(sym_up);
        op.type = OT_MEM;
        if (idx == -1) {
            op.sym_unresolved = 1;
            strncpy(op.sym, sym_up, MAX_TOKEN-1);
        } else {
            op.value = sym_table[idx].offset;
            strncpy(op.sym, sym_up, MAX_TOKEN-1);
        }
        return op;
    }
}

/* ================================================================== */
/*  Object code emission helpers                                        */
/* ================================================================== */

/* Write byte(s) into the flat buffer at the correct segment offset */
static void emit_byte(SegID seg, int *lc, unsigned char b, ListEntry *le) {
    int flat = (seg == SEG_DATA) ? (obj_data_start + *lc)
                                 : (obj_code_start + *lc);
    if (flat >= 0 && flat < MAX_OBJ_BYTES) obj_data[flat] = b;
    if (le && le->byte_count < 16) le->bytes[le->byte_count++] = b;
    (*lc)++;
}

static void emit_word(SegID seg, int *lc, unsigned short w, ListEntry *le) {
    emit_byte(seg, lc, (unsigned char)(w & 0xFF), le);
    emit_byte(seg, lc, (unsigned char)((w >> 8) & 0xFF), le);
}

/* ================================================================== */
/*  Instruction Encoder (Pass 2)                                        */
/* ================================================================== */

/*
 * Encode one instruction and emit bytes into obj_data.
 * Returns number of bytes emitted, or -1 on error.
 *
 * We implement a representative subset.  Real 8086 encoding is much
 * larger; this covers the instructions required for typical lab programs.
 */
static int encode_instruction(const char *mnemonic,
                               const char *op1_str,
                               const char *op2_str,
                               int line_no,
                               ListEntry *le)
{
    char mn[MAX_TOKEN]; strncpy(mn, mnemonic, MAX_TOKEN-1); str_toupper(mn);

    Operand op1 = {OT_NONE,0,0,0,0,{0},0};
    Operand op2 = {OT_NONE,0,0,0,0,{0},0};
    if (op1_str && op1_str[0]) op1 = parse_operand(op1_str, line_no);
    if (op2_str && op2_str[0]) op2 = parse_operand(op2_str, line_no);

    int start_lc = lc_code;

    /* ---- NOP / HLT ---- */
    if (strcmp(mn,"NOP")==0) { emit_byte(SEG_CODE,&lc_code,0x90,le); return lc_code-start_lc; }
    if (strcmp(mn,"HLT")==0) { emit_byte(SEG_CODE,&lc_code,0xF4,le); return lc_code-start_lc; }
    if (strcmp(mn,"RET")==0) { emit_byte(SEG_CODE,&lc_code,0xC3,le); return lc_code-start_lc; }

    /* ---- INT ---- */
    if (strcmp(mn,"INT")==0) {
        emit_byte(SEG_CODE,&lc_code,0xCD,le);
        emit_byte(SEG_CODE,&lc_code,(unsigned char)(op1.value & 0xFF),le);
        return lc_code-start_lc;
    }

    /* ---- PUSH / POP ---- */
    if (strcmp(mn,"PUSH")==0) {
        if (op1.type==OT_REG && !op1.is8bit && !op1.isseg)
            emit_byte(SEG_CODE,&lc_code,(unsigned char)(0x50+op1.reg_code),le);
        else if (op1.type==OT_REG && op1.isseg)
            emit_byte(SEG_CODE,&lc_code,(unsigned char)(0x06+(op1.reg_code<<3)),le);
        else if (op1.type==OT_IMM) {
            emit_byte(SEG_CODE,&lc_code,0x68,le);
            emit_word(SEG_CODE,&lc_code,(unsigned short)op1.value,le);
        } else { add_error(line_no,"PUSH: unsupported operand"); }
        return lc_code-start_lc;
    }
    if (strcmp(mn,"POP")==0) {
        if (op1.type==OT_REG && !op1.is8bit && !op1.isseg)
            emit_byte(SEG_CODE,&lc_code,(unsigned char)(0x58+op1.reg_code),le);
        else if (op1.type==OT_REG && op1.isseg)
            emit_byte(SEG_CODE,&lc_code,(unsigned char)(0x07+(op1.reg_code<<3)),le);
        else { add_error(line_no,"POP: unsupported operand"); }
        return lc_code-start_lc;
    }

    /* ---- MOV ---- */
    if (strcmp(mn,"MOV")==0) {
        /* MOV reg, imm */
        if (op1.type==OT_REG && op2.type==OT_IMM) {
            if (op1.is8bit) {
                emit_byte(SEG_CODE,&lc_code,(unsigned char)(0xB0+op1.reg_code),le);
                emit_byte(SEG_CODE,&lc_code,(unsigned char)(op2.value&0xFF),le);
            } else {
                emit_byte(SEG_CODE,&lc_code,(unsigned char)(0xB8+op1.reg_code),le);
                emit_word(SEG_CODE,&lc_code,(unsigned short)op2.value,le);
            }
        }
        /* MOV reg, reg */
        else if (op1.type==OT_REG && op2.type==OT_REG && !op1.is8bit && !op2.is8bit) {
            emit_byte(SEG_CODE,&lc_code,0x89,le);
            emit_byte(SEG_CODE,&lc_code,(unsigned char)(0xC0|(op1.reg_code)|(op2.reg_code<<3)),le);
        }
        /* MOV reg, [mem] */
        else if (op1.type==OT_REG && op2.type==OT_MEM && !op1.is8bit) {
            emit_byte(SEG_CODE,&lc_code,0x8B,le);
            emit_byte(SEG_CODE,&lc_code,(unsigned char)(0x06|(op1.reg_code<<3)),le);
            emit_word(SEG_CODE,&lc_code,(unsigned short)op2.value,le);
        }
        /* MOV [mem], reg */
        else if (op1.type==OT_MEM && op2.type==OT_REG && !op2.is8bit) {
            emit_byte(SEG_CODE,&lc_code,0x89,le);
            emit_byte(SEG_CODE,&lc_code,(unsigned char)(0x06|(op2.reg_code<<3)),le);
            emit_word(SEG_CODE,&lc_code,(unsigned short)op1.value,le);
        }
        /* MOV reg, [reg] (register indirect) */
        else if (op1.type==OT_REG && op2.type==OT_REGIND) {
            emit_byte(SEG_CODE,&lc_code,0x8B,le);
            emit_byte(SEG_CODE,&lc_code,(unsigned char)((op1.reg_code<<3)|op2.reg_code),le);
        }
        /* MOV [reg], reg (register indirect dest) */
        else if (op1.type==OT_REGIND && op2.type==OT_REG) {
            emit_byte(SEG_CODE,&lc_code,0x89,le);
            emit_byte(SEG_CODE,&lc_code,(unsigned char)((op2.reg_code<<3)|op1.reg_code),le);
        }
        /* MOV seg_reg, reg */
        else if (op1.type==OT_REG && op1.isseg && op2.type==OT_REG) {
            emit_byte(SEG_CODE,&lc_code,0x8E,le);
            emit_byte(SEG_CODE,&lc_code,(unsigned char)(0xC0|(op1.reg_code<<3)|op2.reg_code),le);
        }
        else { add_error(line_no,"MOV: unsupported operand combination"); }
        return lc_code-start_lc;
    }

    /* ---- ADD / SUB / AND / OR / XOR / CMP ---- */
    /* We use the Opcode /r and Opcode /imm forms  */
    {
        typedef struct { const char *mn; unsigned char op_rr; unsigned char op_ri8; unsigned char op_ri16; unsigned char reg_field; } AluOp;
        static const AluOp alu_ops[] = {
            {"ADD", 0x01, 0x83, 0x81, 0},
            {"SUB", 0x29, 0x83, 0x81, 5},
            {"AND", 0x21, 0x83, 0x81, 4},
            {"OR",  0x09, 0x83, 0x81, 1},
            {"XOR", 0x31, 0x83, 0x81, 6},
            {"CMP", 0x39, 0x83, 0x81, 7},
            {NULL,0,0,0,0}
        };
        for (int i = 0; alu_ops[i].mn; i++) {
            if (strcmp(mn, alu_ops[i].mn) == 0) {
                if (op1.type==OT_REG && op2.type==OT_REG) {
                    emit_byte(SEG_CODE,&lc_code,alu_ops[i].op_rr,le);
                    emit_byte(SEG_CODE,&lc_code,(unsigned char)(0xC0|(op2.reg_code<<3)|op1.reg_code),le);
                } else if (op1.type==OT_REG && op2.type==OT_IMM) {
                    int imm = op2.value;
                    if (imm >= -128 && imm <= 127) {
                        emit_byte(SEG_CODE,&lc_code,0x83,le);
                        emit_byte(SEG_CODE,&lc_code,(unsigned char)(0xC0|(alu_ops[i].reg_field<<3)|op1.reg_code),le);
                        emit_byte(SEG_CODE,&lc_code,(unsigned char)(imm&0xFF),le);
                    } else {
                        emit_byte(SEG_CODE,&lc_code,0x81,le);
                        emit_byte(SEG_CODE,&lc_code,(unsigned char)(0xC0|(alu_ops[i].reg_field<<3)|op1.reg_code),le);
                        emit_word(SEG_CODE,&lc_code,(unsigned short)imm,le);
                    }
                } else if (op1.type==OT_REG && op2.type==OT_MEM) {
                    emit_byte(SEG_CODE,&lc_code,(unsigned char)(alu_ops[i].op_rr-1),le); /* 8B form */
                    emit_byte(SEG_CODE,&lc_code,(unsigned char)(0x06|(op1.reg_code<<3)),le);
                    emit_word(SEG_CODE,&lc_code,(unsigned short)op2.value,le);
                } else { add_error(line_no,"%s: unsupported operand combination",mn); }
                return lc_code-start_lc;
            }
        }
    }

    /* ---- INC / DEC ---- */
    if (strcmp(mn,"INC")==0 || strcmp(mn,"DEC")==0) {
        int base = (strcmp(mn,"INC")==0) ? 0x40 : 0x48;
        if (op1.type==OT_REG && !op1.is8bit)
            emit_byte(SEG_CODE,&lc_code,(unsigned char)(base+op1.reg_code),le);
        else if (op1.type==OT_REG && op1.is8bit) {
            emit_byte(SEG_CODE,&lc_code,(strcmp(mn,"INC")==0)?0xFE:0xFE,le);
            emit_byte(SEG_CODE,&lc_code,(unsigned char)(0xC0|((strcmp(mn,"INC")==0)?0:8)|op1.reg_code),le);
        } else { add_error(line_no,"%s: unsupported operand",mn); }
        return lc_code-start_lc;
    }

    /* ---- MUL / DIV (unsigned, AX = AX * src  / AX = AX / src) ---- */
    if (strcmp(mn,"MUL")==0 || strcmp(mn,"DIV")==0) {
        unsigned char reg_f = (strcmp(mn,"MUL")==0) ? 4 : 6;
        if (op1.type==OT_REG && !op1.is8bit) {
            emit_byte(SEG_CODE,&lc_code,0xF7,le);
            emit_byte(SEG_CODE,&lc_code,(unsigned char)(0xC0|(reg_f<<3)|op1.reg_code),le);
        } else { add_error(line_no,"%s: unsupported operand",mn); }
        return lc_code-start_lc;
    }

    /* ---- NOT / NEG ---- */
    if (strcmp(mn,"NOT")==0 || strcmp(mn,"NEG")==0) {
        unsigned char reg_f = (strcmp(mn,"NOT")==0) ? 2 : 3;
        if (op1.type==OT_REG && !op1.is8bit) {
            emit_byte(SEG_CODE,&lc_code,0xF7,le);
            emit_byte(SEG_CODE,&lc_code,(unsigned char)(0xC0|(reg_f<<3)|op1.reg_code),le);
        } else { add_error(line_no,"%s: unsupported operand",mn); }
        return lc_code-start_lc;
    }

    /* ---- SHL / SHR ---- */
    if (strcmp(mn,"SHL")==0 || strcmp(mn,"SHR")==0) {
        unsigned char reg_f = (strcmp(mn,"SHL")==0) ? 4 : 5;
        if (op1.type==OT_REG && op2.type==OT_IMM) {
            if (op2.value == 1) {
                emit_byte(SEG_CODE,&lc_code,0xD1,le);
                emit_byte(SEG_CODE,&lc_code,(unsigned char)(0xC0|(reg_f<<3)|op1.reg_code),le);
            } else {
                emit_byte(SEG_CODE,&lc_code,0xC1,le);
                emit_byte(SEG_CODE,&lc_code,(unsigned char)(0xC0|(reg_f<<3)|op1.reg_code),le);
                emit_byte(SEG_CODE,&lc_code,(unsigned char)(op2.value&0xFF),le);
            }
        } else { add_error(line_no,"%s: unsupported operand combination",mn); }
        return lc_code-start_lc;
    }

    /* ---- Jumps ---- */
    {
        typedef struct { const char *mn; unsigned char opcode; } JmpOp;
        static const JmpOp jmps[] = {
            {"JMP",0xE9},{"JE",0x74},{"JNE",0x75},
            {"JZ",0x74},{"JNZ",0x75},
            {"JL",0x7C},{"JG",0x7F},
            {"JLE",0x7E},{"JGE",0x7D},
            {"JA",0x77},{"JB",0x72},
            {NULL,0}
        };
        for (int i = 0; jmps[i].mn; i++) {
            if (strcmp(mn, jmps[i].mn) == 0) {
                int target = 0;
                if (op1.type==OT_MEM || (op1.sym[0] && !op1.sym_unresolved)) {
                    target = op1.value;
                } else if (op1.type==OT_IMM) {
                    target = op1.value;
                }
                if (jmps[i].opcode == 0xE9) {
                    /* near jump: 3 bytes total, rel16 */
                    emit_byte(SEG_CODE,&lc_code,0xE9,le);
                    int rel = target - (lc_code + 2);
                    emit_word(SEG_CODE,&lc_code,(unsigned short)(rel),le);
                } else {
                    /* short conditional jump: 2 bytes, rel8 */
                    emit_byte(SEG_CODE,&lc_code,jmps[i].opcode,le);
                    int rel = target - (lc_code + 1);
                    emit_byte(SEG_CODE,&lc_code,(unsigned char)(rel&0xFF),le);
                }
                return lc_code-start_lc;
            }
        }
    }

    /* ---- CALL ---- */
    if (strcmp(mn,"CALL")==0) {
        int target = (op1.type==OT_MEM || op1.type==OT_IMM) ? op1.value : 0;
        emit_byte(SEG_CODE,&lc_code,0xE8,le);
        int rel = target - (lc_code + 2);
        emit_word(SEG_CODE,&lc_code,(unsigned short)rel,le);
        return lc_code-start_lc;
    }

    add_error(line_no, "Unknown or unsupported instruction '%s'", mn);
    return 0;
}

/* ================================================================== */
/*  Directive size calculation (pass 1 only)                           */
/* ================================================================== */
static int directive_size(const char *dir, const char *operand) {
    char up[MAX_TOKEN]; strncpy(up, dir, MAX_TOKEN-1); str_toupper(up);
    if (strcmp(up,"DB")==0) {
        /* Count comma-separated items; strings count each char */
        int count = 0;
        const char *p = operand;
        int in_str = 0;
        while (*p) {
            if (*p == '"' || *p == '\'') { in_str = !in_str; if (!in_str) count++; p++; continue; }
            if (in_str) { count++; p++; continue; }
            if (*p == ',') { p++; continue; }
            /* skip whitespace and numeric tokens */
            while (*p && *p != ',' && *p != '"' && *p != '\'') p++;
            if (*(p-1) != ',') count++;
        }
        return (count == 0) ? 1 : count;
    }
    if (strcmp(up,"DW")==0) {
        /* count commas + 1, or handle DUP */
        const char *dup = strstr(operand, "DUP");
        if (!dup) dup = strstr(operand, "dup");
        if (dup) {
            int n = (int)strtol(operand, NULL, 10);
            return n * 2;
        }
        int commas = 0;
        for (const char *q = operand; *q; q++) if (*q == ',') commas++;
        return (commas + 1) * 2;
    }
    if (strcmp(up,"DD")==0) {
        int commas = 0;
        for (const char *q = operand; *q; q++) if (*q == ',') commas++;
        return (commas + 1) * 4;
    }
    return 0;
}

/* ================================================================== */
/*  Process a DATA directive in Pass 2 (emit bytes)                    */
/* ================================================================== */
static void emit_data_directive(const char *dir, const char *operand, int line_no, ListEntry *le) {
    char up[MAX_TOKEN]; strncpy(up, dir, MAX_TOKEN-1); str_toupper(up);

    if (strcmp(up,"DB")==0) {
        const char *p = operand;
        while (*p) {
            p = ltrim((char*)p);
            if (*p == '"' || *p == '\'') {
                char delim = *p++; 
                while (*p && *p != delim) {
                    emit_byte(SEG_DATA,&lc_data,(unsigned char)*p,le); p++;
                }
                if (*p) p++; /* closing quote */
            } else {
                char nbuf[MAX_TOKEN]; int ni = 0;
                while (*p && *p != ',' && !isspace((unsigned char)*p)) nbuf[ni++] = *p++;
                nbuf[ni] = '\0';
                if (ni > 0) emit_byte(SEG_DATA,&lc_data,(unsigned char)(strtol(nbuf,NULL,0)&0xFF),le);
            }
            p = ltrim((char*)p);
            if (*p == ',') p++;
        }
        return;
    }
    if (strcmp(up,"DW")==0) {
        /* handle DUP */
        const char *dup = strstr(operand,"DUP"); if (!dup) dup = strstr(operand,"dup");
        if (dup) {
            int n = (int)strtol(operand, NULL, 10);
            const char *inner_start = strchr(dup, '(');
            int val = 0;
            if (inner_start) val = (int)strtol(inner_start+1, NULL, 0);
            for (int k = 0; k < n; k++) emit_word(SEG_DATA,&lc_data,(unsigned short)val,le);
            return;
        }
        const char *p = operand;
        while (*p) {
            p = ltrim((char*)p);
            if (*p == '?') { emit_word(SEG_DATA,&lc_data,0,le); p++; }
            else {
                char nbuf[MAX_TOKEN]; int ni = 0;
                while (*p && *p != ',') nbuf[ni++] = *p++;
                nbuf[ni] = '\0';
                trim(nbuf);
                if (ni > 0) emit_word(SEG_DATA,&lc_data,(unsigned short)(strtol(nbuf,NULL,0)&0xFFFF),le);
            }
            p = ltrim((char*)p);
            if (*p == ',') p++;
        }
        return;
    }
    if (strcmp(up,"DD")==0) {
        const char *p = operand;
        while (*p) {
            p = ltrim((char*)p);
            char nbuf[MAX_TOKEN]; int ni = 0;
            while (*p && *p != ',') nbuf[ni++] = *p++;
            nbuf[ni] = '\0'; trim(nbuf);
            if (ni > 0) {
                unsigned long val = strtoul(nbuf, NULL, 0);
                emit_byte(SEG_DATA,&lc_data,(unsigned char)(val&0xFF),le);
                emit_byte(SEG_DATA,&lc_data,(unsigned char)((val>>8)&0xFF),le);
                emit_byte(SEG_DATA,&lc_data,(unsigned char)((val>>16)&0xFF),le);
                emit_byte(SEG_DATA,&lc_data,(unsigned char)((val>>24)&0xFF),le);
            }
            p = ltrim((char*)p);
            if (*p == ',') p++;
        }
        return;
    }
    (void)line_no;
}

/* ================================================================== */
/*  Tokenise a source line                                              */
/*  Returns: label (may be ""), mnemonic, op1, op2                     */
/* ================================================================== */
static void tokenise(const char *line,
                     char *label, char *mnemonic, char *op1, char *op2)
{
    label[0] = mnemonic[0] = op1[0] = op2[0] = '\0';

    char buf[MAX_LINE]; strncpy(buf, line, MAX_LINE-1);

    /* strip comment */
    char *semi = strchr(buf, ';'); if (semi) *semi = '\0';
    char *p = trim(buf);
    if (!p || *p == '\0') return;

    /* label check: first token ends with ':' */
    char *colon = strchr(p, ':');
    /* Also check: if first token is followed by a known pseudo (PROC,ENDP,EQU) it acts as a label */
    if (colon) {
        int ll = (int)(colon - p);
        if (ll > 0) { strncpy(label, p, (size_t)ll); label[ll] = '\0'; trim(label); str_toupper(label); }
        p = ltrim(colon + 1);
    } else {
        /* Check if line starts with an identifier followed by PROC / ENDP / EQU */
        char first[MAX_TOKEN] = "", second[MAX_TOKEN] = "";
        char tmpbuf[MAX_LINE]; strncpy(tmpbuf, p, MAX_LINE-1);
        char *t1 = strtok(tmpbuf, " \t"); if (t1) strncpy(first, t1, MAX_TOKEN-1);
        char *t2 = strtok(NULL,   " \t"); if (t2) strncpy(second, t2, MAX_TOKEN-1);
        char s2up[MAX_TOKEN]; strncpy(s2up, second, MAX_TOKEN-1); str_toupper(s2up);
        if (second[0] && (strcmp(s2up,"PROC")==0||strcmp(s2up,"ENDP")==0||strcmp(s2up,"EQU")==0||
                          strcmp(s2up,"DB")==0||strcmp(s2up,"DW")==0||strcmp(s2up,"DD")==0)) {
            strncpy(label, first, MAX_TOKEN-1); str_toupper(label);
            /* advance p past the first token */
            while (*p && !isspace((unsigned char)*p)) p++;
            p = ltrim(p);
        }
    }

    /* mnemonic */
    char *space = p;
    while (*space && !isspace((unsigned char)*space)) space++;
    int ml = (int)(space - p);
    if (ml > 0) { strncpy(mnemonic, p, (size_t)ml); mnemonic[ml] = '\0'; str_toupper(mnemonic); }
    p = ltrim(space);

    /* operands: split on first comma not inside brackets/parens */
    /* Simple approach: find comma at depth 0 */
    if (*p == '\0') return;
    int depth = 0; const char *comma = NULL;
    for (const char *q = p; *q; q++) {
        if (*q=='['||*q=='(') depth++;
        else if (*q==']'||*q==')') depth--;
        else if (*q==',' && depth==0) { comma = q; break; }
    }
    if (comma) {
        int l1 = (int)(comma - p);
        strncpy(op1, p, (size_t)l1); op1[l1] = '\0'; trim(op1);
        strncpy(op2, comma+1, MAX_TOKEN-1); trim(op2);
    } else {
        strncpy(op1, p, MAX_TOKEN-1); trim(op1);
    }
}

/* ================================================================== */
/*  Check if mnemonic is a directive                                    */
/* ================================================================== */
static int is_data_directive(const char *mn) {
    return strcmp(mn,"DB")==0||strcmp(mn,"DW")==0||strcmp(mn,"DD")==0;
}
static int is_pseudo_op(const char *mn) {
    return strcmp(mn,".MODEL")==0||strcmp(mn,".STACK")==0||
           strcmp(mn,".DATA")==0||strcmp(mn,".CODE")==0||
           strcmp(mn,"ENDS")==0||strcmp(mn,"END")==0||
           strcmp(mn,"ORG")==0||strcmp(mn,"EQU")==0||
           strcmp(mn,"ASSUME")==0||strcmp(mn,"PROC")==0||
           strcmp(mn,"ENDP")==0||
           is_data_directive(mn);
}

/* Estimate instruction size for pass 1 (conservative) */
static int estimate_instr_size(const char *mn, const char *op1, const char *op2) {
    /* Most instructions: 1-6 bytes; we use a simple heuristic */
    if (strcmp(mn,"NOP")==0||strcmp(mn,"HLT")==0||strcmp(mn,"RET")==0) return 1;
    if (strcmp(mn,"INT")==0) return 2;
    if (strcmp(mn,"PUSH")==0||strcmp(mn,"POP")==0) return 1;
    if (strcmp(mn,"INC")==0||strcmp(mn,"DEC")==0) return 2;
    if (strcmp(mn,"MUL")==0||strcmp(mn,"DIV")==0) return 2;
    if (strcmp(mn,"NOT")==0||strcmp(mn,"NEG")==0) return 2;
    /* Jumps */
    if (strncmp(mn,"J",1)==0||strcmp(mn,"CALL")==0) return 3;
    /* MOV reg, imm16 */
    if (strcmp(mn,"MOV")==0) {
        const RegEntry *r = find_reg(op1);
        if (r && !r->is8bit && strstr(op2,"AX")==NULL && find_reg(op2)==NULL) return 3;
        if (r && r->is8bit) return 2;
        /* mov reg, [mem] or [mem], reg */
        if (op2[0]=='[' || op1[0]=='[') return 4;
        return 2;
    }
    /* ALU with imm16 */
    if (op2 && (isdigit((unsigned char)op2[0]) || op2[0]=='-')) return 4;
    return 3; /* default */
}

/* ================================================================== */
/*  PASS 1 – Build symbol table, compute LC values                     */
/* ================================================================== */
static void pass1(void)
{
    cur_seg  = SEG_NONE;
    lc_data  = 0;
    lc_code  = 0;
    lc_stack = 0;

    /* Reserve space layout:
     *   [0 .. stack_size-1]  : stack area (not in obj, just reserved)
     *   [stack_size .. ]     : data segment
     *   [stack_size+data .. ]: code segment
     * We'll fix this after pass 1.
     */

    for (int i = 0; i < src_count; i++) {
        char label[MAX_TOKEN], mnemonic[MAX_TOKEN], op1[MAX_TOKEN], op2[MAX_TOKEN];
        tokenise(src_lines[i].text, label, mnemonic, op1, op2);
        int ln = src_lines[i].line_no;

        if (mnemonic[0] == '\0' && label[0] == '\0') continue;

        /* Segment directives */
        if (strcmp(mnemonic,".DATA")==0)  { cur_seg = SEG_DATA;  continue; }
        if (strcmp(mnemonic,".CODE")==0)  { cur_seg = SEG_CODE;  continue; }
        if (strcmp(mnemonic,".STACK")==0) {
            cur_seg = SEG_STACK;
            if (op1[0]) { lc_stack = (int)strtol(op1, NULL, 0); obj_stack_size = lc_stack; }
            else { obj_stack_size = 256; }
            continue;
        }
        if (strcmp(mnemonic,".MODEL")==0||strcmp(mnemonic,"ASSUME")==0||
            strcmp(mnemonic,"ENDS")==0||strcmp(mnemonic,"PROC")==0||
            strcmp(mnemonic,"ENDP")==0) continue;
        if (strcmp(mnemonic,"END")==0) break;

        /* ORG */
        if (strcmp(mnemonic,"ORG")==0) {
            int val = (int)strtol(op1, NULL, 0);
            if (cur_seg==SEG_DATA) lc_data = val;
            else if (cur_seg==SEG_CODE) lc_code = val;
            continue;
        }

        /* EQU */
        if (strcmp(mnemonic,"EQU")==0) {
            if (label[0]) {
                int val = (int)strtol(op1, NULL, 0);
                sym_add(label, SEG_NONE, val, 0, SYM_EQU, ln);
            }
            continue;
        }

        /* Define label */
        if (label[0]) {
            SegID lseg = cur_seg;
            int   loff = (cur_seg==SEG_DATA) ? lc_data : (cur_seg==SEG_CODE ? lc_code : 0);
            int   sz   = 0;
            SymKind knd = SYM_LABEL;
            if (is_data_directive(mnemonic)) { sz = directive_size(mnemonic, op1); knd = SYM_VAR; }
            sym_add(label, lseg, loff, sz, knd, ln);
        }

        /* Advance LC */
        if (mnemonic[0]) {
            if (is_data_directive(mnemonic)) {
                int sz = directive_size(mnemonic, op1);
                if (cur_seg==SEG_DATA) lc_data += sz;
            } else if (!is_pseudo_op(mnemonic)) {
                int sz = estimate_instr_size(mnemonic, op1, op2);
                if (cur_seg==SEG_CODE) lc_code += sz;
            }
        }
    }

    /* After pass 1: set segment base addresses in flat buffer
     * Stack | Data | Code (contiguous, for a simple flat .com-style)
     */
    obj_data_start  = obj_stack_size;
    obj_code_start  = obj_stack_size + lc_data;
}

/* ================================================================== */
/*  PASS 2 – Generate object code                                       */
/* ================================================================== */
static void pass2(void)
{
    cur_seg = SEG_NONE;
    lc_data = 0;
    lc_code = 0;

    for (int i = 0; i < src_count; i++) {
        char label[MAX_TOKEN], mnemonic[MAX_TOKEN], op1[MAX_TOKEN], op2[MAX_TOKEN];
        char src_copy[MAX_LINE]; strncpy(src_copy, src_lines[i].text, MAX_LINE-1); trim(src_copy);
        tokenise(src_lines[i].text, label, mnemonic, op1, op2);
        int ln = src_lines[i].line_no;

        /* Prepare listing entry */
        ListEntry le;
        memset(&le, 0, sizeof(le));
        le.line_no   = ln;
        le.seg       = cur_seg;
        le.seg_offset = -1; /* means no code */
        strncpy(le.source, src_copy[0] ? src_copy : "(blank)", MAX_LINE-1);

        if (mnemonic[0] == '\0' && label[0] == '\0') {
            list_table[list_count++] = le;
            continue;
        }

        /* Segment control */
        if (strcmp(mnemonic,".DATA")==0)  { cur_seg=SEG_DATA;  le.seg=SEG_DATA;  list_table[list_count++]=le; continue; }
        if (strcmp(mnemonic,".CODE")==0)  { cur_seg=SEG_CODE;  le.seg=SEG_CODE;  list_table[list_count++]=le; continue; }
        if (strcmp(mnemonic,".STACK")==0) { cur_seg=SEG_STACK; le.seg=SEG_STACK; list_table[list_count++]=le; continue; }
        if (strcmp(mnemonic,".MODEL")==0||strcmp(mnemonic,"ASSUME")==0||
            strcmp(mnemonic,"ENDS")==0||strcmp(mnemonic,"PROC")==0||
            strcmp(mnemonic,"ENDP")==0) { list_table[list_count++]=le; continue; }
        if (strcmp(mnemonic,"END")==0)    { list_table[list_count++]=le; break; }

        if (strcmp(mnemonic,"ORG")==0) {
            int val = (int)strtol(op1, NULL, 0);
            if (cur_seg==SEG_DATA) lc_data=val; else if (cur_seg==SEG_CODE) lc_code=val;
            list_table[list_count++]=le; continue;
        }
        if (strcmp(mnemonic,"EQU")==0) { list_table[list_count++]=le; continue; }

        le.seg = cur_seg;

        if (cur_seg == SEG_DATA && is_data_directive(mnemonic)) {
            le.seg_offset = lc_data;
            emit_data_directive(mnemonic, op1, ln, &le);
            list_table[list_count++] = le;
            continue;
        }

        if (cur_seg == SEG_CODE && mnemonic[0] != '\0' && !is_pseudo_op(mnemonic)) {
            le.seg_offset = lc_code;
            encode_instruction(mnemonic, op1[0]?op1:NULL, op2[0]?op2:NULL, ln, &le);
            list_table[list_count++] = le;
            continue;
        }

        list_table[list_count++] = le;
    }
}

/* ================================================================== */
/*  Write Object File                                                   */
/* ================================================================== */
static void write_object_file(const char *filename)
{
    FILE *f = fopen(filename, "w");
    if (!f) { perror(filename); return; }

    fprintf(f, "; 8086 Object File\n");
    fprintf(f, "; Generated by PR4 Two-Pass Assembler\n");
    fprintf(f, ";\n");
    fprintf(f, "; Stack size : %d bytes (0x%04X – 0x%04X)\n",
            obj_stack_size, 0, obj_stack_size-1);
    fprintf(f, "; Data  seg  : base 0x%04X\n", obj_data_start);
    fprintf(f, "; Code  seg  : base 0x%04X\n", obj_code_start);
    fprintf(f, ";\n");
    fprintf(f, "; Format: SEGMENT  OFFSET  HEXBYTES\n\n");

    /* Data segment */
    for (int off = 0; off < lc_data; off += 16) {
        fprintf(f, "DATA    %04X  ", off);
        for (int k = off; k < off+16 && k < lc_data; k++)
            fprintf(f, "%02X ", obj_data[obj_data_start + k]);
        fprintf(f, "\n");
    }

    /* Code segment */
    for (int off = 0; off < lc_code; off += 16) {
        fprintf(f, "CODE    %04X  ", off);
        for (int k = off; k < off+16 && k < lc_code; k++)
            fprintf(f, "%02X ", obj_data[obj_code_start + k]);
        fprintf(f, "\n");
    }

    fclose(f);
}

/* ================================================================== */
/*  Print Reports                                                       */
/* ================================================================== */

static void print_separator(char c, int n) {
    for (int i = 0; i < n; i++) putchar(c);
    putchar('\n');
}

static void print_symbol_table(void) {
    printf("\n");
    print_separator('=', 70);
    printf("  SYMBOL TABLE\n");
    print_separator('=', 70);
    printf("  %-20s  %-8s  %-10s  %-6s  %s\n",
           "Label", "Segment", "Offset", "Size", "Kind");
    print_separator('-', 70);
    if (sym_count == 0) { printf("  (empty)\n"); }
    for (int i = 0; i < sym_count; i++) {
        Symbol *s = &sym_table[i];
        const char *kind_str = (s->kind==SYM_EQU)?"EQU":(s->kind==SYM_VAR)?"VAR":"LABEL";
        printf("  %-20s  %-8s  0x%04X      %-6d  %s\n",
               s->name, seg_name(s->segment), s->offset, s->size, kind_str);
    }
    print_separator('=', 70);
}

static void print_error_table(void) {
    printf("\n");
    print_separator('=', 70);
    printf("  ERROR TABLE\n");
    print_separator('=', 70);
    if (err_count == 0) {
        printf("  No errors. Assembly successful.\n");
    } else {
        printf("  %-6s  %s\n", "Line", "Message");
        print_separator('-', 70);
        for (int i = 0; i < err_count; i++)
            printf("  %-6d  %s\n", err_table[i].line_no, err_table[i].message);
    }
    print_separator('=', 70);
}

static void print_listing(void) {
    printf("\n");
    print_separator('=', 70);
    printf("  OBJECT LISTING\n");
    print_separator('=', 70);
    printf("  %-4s  %-5s  %-4s  %-22s  %s\n",
           "Line", "Seg", "Off.", "Hex Bytes", "Source");
    print_separator('-', 70);

    for (int i = 0; i < list_count; i++) {
        ListEntry *le = &list_table[i];
        char hex_buf[64] = "";
        for (int k = 0; k < le->byte_count && k < 8; k++) {
            char tmp[4]; snprintf(tmp, sizeof(tmp), "%02X ", le->bytes[k]);
            strncat(hex_buf, tmp, sizeof(hex_buf)-strlen(hex_buf)-1);
        }
        if (le->seg_offset >= 0)
            printf("  %-4d  %-5s  %04X  %-22s  %s\n",
                   le->line_no, seg_name(le->seg), le->seg_offset,
                   hex_buf, le->source);
        else
            printf("  %-4d  %-5s  ----  %-22s  %s\n",
                   le->line_no, seg_name(le->seg), "", le->source);
    }
    print_separator('=', 70);
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

    FILE *fp = fopen(argv[1], "r");
    if (!fp) { perror(argv[1]); return 1; }

    /* Read all source lines */
    char line[MAX_LINE];
    int line_no = 0;
    while (fgets(line, sizeof(line), fp) && src_count < MAX_SOURCE_LINES) {
        line_no++;
        /* strip newline */
        line[strcspn(line, "\r\n")] = '\0';
        src_lines[src_count].line_no = line_no;
        strncpy(src_lines[src_count].text, line, MAX_LINE-1);
        src_count++;
    }
    fclose(fp);

    /* Header */
    print_separator('=', 70);
    printf("  8086 Two-Pass Assembler  –  PR4\n");
    printf("  Source file : %s   (%d lines)\n", argv[1], src_count);
    print_separator('=', 70);

    /* PASS 1 */
    printf("\n[Pass 1] Building symbol table...\n");
    pass1();
    printf("         %d symbol(s) found.\n", sym_count);
    printf("         Data segment size : %d bytes\n", lc_data);
    printf("         Code segment size : %d bytes\n", lc_code);

    /* PASS 2 */
    printf("\n[Pass 2] Generating object code...\n");
    memset(obj_data, 0, sizeof(obj_data));
    lc_data = lc_code = 0;
    pass2();
    printf("         Object bytes generated.\n");

    /* Reports */
    print_symbol_table();
    print_error_table();
    print_listing();

    /* Write .obj */
    printf("\n");
    if (err_count == 0) {
        const char *outfile = "output8086.obj";
        write_object_file(outfile);
        printf("  Object file written: %s\n", outfile);
    } else {
        printf("  %d error(s) found – object file NOT written.\n", err_count);
    }
    print_separator('=', 70);

    return err_count ? 1 : 0;
}
