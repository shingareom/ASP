/*
 * ============================================================
 *  PR5 – 8086 Two-Pass Assembler with Procedure Support
 * ============================================================
 *
 *  Extends PR4 with full procedure (subroutine) handling:
 *
 *  NEW in PR5
 *  ----------
 *  Procedure Table
 *      Each PROC/ENDP pair is recorded:
 *        name, segment, start offset, end offset, call count,
 *        type (NEAR / FAR)
 *
 *  Procedure directives
 *      NAME  PROC  [NEAR|FAR]   – begin procedure
 *      NAME  ENDP               – end procedure
 *
 *  Stack-frame conventions (auto-generated prologue/epilogue)
 *      If a procedure is declared with NEAR or FAR, the assembler
 *      emits the standard 8086 stack frame:
 *        Prologue: PUSH BP  /  MOV BP, SP
 *        Epilogue: POP  BP  /  RET
 *      Local variable allocation via LOCAL directive:
 *        LOCAL  varname:type[,varname:type ...]
 *        Reserves space on stack (SUB SP, n) and records
 *        each local as [BP-offset] in the procedure's local table.
 *
 *  ENTER / LEAVE instructions (8086 extensions)
 *      ENTER  framesize, nestlevel
 *      LEAVE
 *
 *  Enhanced CALL / RET
 *      CALL  procname        – near call (resolves to proc entry point)
 *      CALL  FAR procname    – far call (inter-segment)
 *      RET   [n]             – near return, optionally pops n bytes
 *      RETF  [n]             – far return
 *
 *  Procedure Report
 *      Printed after assembly: name, type, start, end, size, calls
 *
 *  All PR4 features retained:
 *  --------------------------
 *  • Two-pass assembly
 *  • .DATA / .CODE / .STACK segments
 *  • Full symbol table + error table
 *  • DB / DW / DD / DUP / EQU / ORG
 *  • MOV, ADD, SUB, MUL, DIV, INC, DEC
 *    AND, OR, XOR, NOT, NEG, SHL, SHR, CMP
 *    PUSH, POP, INT, NOP, HLT
 *    JMP/Jcc, CALL, RET, RETF
 *  • Register, immediate, direct, register-indirect addressing
 *  • Object listing + .obj output file
 *
 *  Usage
 *  -----
 *    ./assembler8086_pr5  input.asm
 *
 *  Build
 *  -----
 *    gcc -Wall -Wextra -o assembler8086_pr5 assembler8086_pr5.c
 * ============================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

/* ------------------------------------------------------------------ */
/*  Limits                                                              */
/* ------------------------------------------------------------------ */
#define MAX_LINE          256
#define MAX_TOKEN         64
#define MAX_SYMBOLS       512
#define MAX_ERRORS        256
#define MAX_OBJ_BYTES     65536
#define MAX_SOURCE_LINES  4096
#define MAX_PROCS         128
#define MAX_LOCALS        32      /* local vars per procedure           */
#define UNDEF_ADDR        -1

/* ------------------------------------------------------------------ */
/*  Segment IDs                                                         */
/* ------------------------------------------------------------------ */
typedef enum { SEG_NONE=0, SEG_DATA, SEG_CODE, SEG_STACK } SegID;

static const char *seg_name(SegID s){
    switch(s){ case SEG_DATA:  return "DATA";
               case SEG_CODE:  return "CODE";
               case SEG_STACK: return "STACK";
               default:        return "NONE"; }
}

/* ------------------------------------------------------------------ */
/*  Symbol Table                                                        */
/* ------------------------------------------------------------------ */
typedef enum { SYM_LABEL, SYM_EQU, SYM_VAR, SYM_PROC } SymKind;

typedef struct {
    char    name[MAX_TOKEN];
    SegID   segment;
    int     offset;
    int     size;
    SymKind kind;
} Symbol;

static Symbol sym_table[MAX_SYMBOLS];
static int    sym_count = 0;

/* ------------------------------------------------------------------ */
/*  Procedure Table  (NEW in PR5)                                       */
/* ------------------------------------------------------------------ */
typedef enum { PROC_NEAR=0, PROC_FAR } ProcType;

typedef struct {
    char     name[MAX_TOKEN];
    SegID    segment;
    int      start_offset;   /* offset of first instruction            */
    int      end_offset;     /* offset just after ENDP                 */
    ProcType type;           /* NEAR or FAR                            */
    int      call_count;     /* how many times CALLed in this file     */
    int      has_frame;      /* 1 = auto prologue/epilogue emitted     */
    int      frame_size;     /* total bytes allocated for locals       */
    /* Local variable table */
    struct {
        char name[MAX_TOKEN];
        int  bp_offset;   /* negative offset from BP, e.g. -2, -4 … */
        int  size;        /* bytes: 1=BYTE,2=WORD,4=DWORD            */
    } locals[MAX_LOCALS];
    int local_count;
} ProcEntry;

static ProcEntry proc_table[MAX_PROCS];
static int       proc_count = 0;

/* Currently open procedure during assembly (-1 = none) */
static int cur_proc_idx = -1;

/* ------------------------------------------------------------------ */
/*  Error Table                                                         */
/* ------------------------------------------------------------------ */
typedef struct { int line_no; char message[MAX_LINE]; } ErrorEntry;
static ErrorEntry err_table[MAX_ERRORS];
static int        err_count = 0;

/* ------------------------------------------------------------------ */
/*  Object buffers / location counters                                  */
/* ------------------------------------------------------------------ */
static unsigned char obj_buf[MAX_OBJ_BYTES];
static int  obj_data_start = 0;
static int  obj_code_start = 0;
static int  obj_stack_size = 256;

static int   lc_data  = 0;
static int   lc_code  = 0;
static int   lc_stack = 0;
static SegID cur_seg  = SEG_NONE;

/* ------------------------------------------------------------------ */
/*  Source lines                                                        */
/* ------------------------------------------------------------------ */
typedef struct { char text[MAX_LINE]; int line_no; } SrcLine;
static SrcLine src_lines[MAX_SOURCE_LINES];
static int     src_count = 0;

/* ------------------------------------------------------------------ */
/*  Object listing                                                      */
/* ------------------------------------------------------------------ */
typedef struct {
    int           seg_offset;
    SegID         seg;
    unsigned char bytes[16];
    int           byte_count;
    char          source[MAX_LINE];
    int           line_no;
    char          proc_marker[MAX_TOKEN]; /* non-empty if proc boundary */
} ListEntry;

static ListEntry list_table[MAX_SOURCE_LINES];
static int       list_count = 0;

/* ================================================================== */
/*  Utilities                                                           */
/* ================================================================== */
static void str_toupper(char *s){ for(;*s;s++) *s=(char)toupper((unsigned char)*s); }
static char *ltrim(char *s){ while(isspace((unsigned char)*s)) s++; return s; }
static char *trim(char *s){
    s=ltrim(s);
    int l=(int)strlen(s);
    while(l>0 && isspace((unsigned char)s[l-1])) s[--l]='\0';
    return s;
}

static void add_error(int line_no, const char *fmt, ...){
    if(err_count>=MAX_ERRORS) return;
    err_table[err_count].line_no=line_no;
    va_list ap; va_start(ap,fmt);
    vsnprintf(err_table[err_count].message,MAX_LINE,fmt,ap);
    va_end(ap); err_count++;
}

/* ================================================================== */
/*  Register table                                                      */
/* ================================================================== */
typedef struct { const char *name; int code; int is8bit; int isseg; } RegEntry;
static const RegEntry reg_table[]={
    {"AX",0,0,0},{"BX",1,0,0},{"CX",2,0,0},{"DX",3,0,0},
    {"SP",4,0,0},{"BP",5,0,0},{"SI",6,0,0},{"DI",7,0,0},
    {"AL",0,1,0},{"CL",1,1,0},{"DL",2,1,0},{"BL",3,1,0},
    {"AH",4,1,0},{"CH",5,1,0},{"DH",6,1,0},{"BH",7,1,0},
    {"ES",0,0,1},{"CS",1,0,1},{"SS",2,0,1},{"DS",3,0,1},
    {NULL,0,0,0}
};
static const RegEntry *find_reg(const char *n){
    char up[MAX_TOKEN]; strncpy(up,n,MAX_TOKEN-1); str_toupper(up);
    for(int i=0;reg_table[i].name;i++)
        if(strcmp(reg_table[i].name,up)==0) return &reg_table[i];
    return NULL;
}

/* ================================================================== */
/*  Symbol Table Operations                                             */
/* ================================================================== */
static int sym_find(const char *n){
    char up[MAX_TOKEN]; strncpy(up,n,MAX_TOKEN-1); str_toupper(up);
    for(int i=0;i<sym_count;i++)
        if(strcmp(sym_table[i].name,up)==0) return i;
    return -1;
}

static int sym_add(const char *n, SegID seg, int off, int sz, SymKind kind, int ln){
    char up[MAX_TOKEN]; strncpy(up,n,MAX_TOKEN-1); str_toupper(up);
    int ex=sym_find(up);
    if(ex!=-1){
        if(sym_table[ex].offset!=UNDEF_ADDR){
            add_error(ln,"Duplicate label '%s'",up); return ex;
        }
        sym_table[ex].segment=seg; sym_table[ex].offset=off;
        sym_table[ex].size=sz;    sym_table[ex].kind=kind;
        return ex;
    }
    if(sym_count>=MAX_SYMBOLS){ add_error(ln,"Symbol table overflow"); return -1; }
    strncpy(sym_table[sym_count].name,up,MAX_TOKEN-1);
    sym_table[sym_count].segment=seg; sym_table[sym_count].offset=off;
    sym_table[sym_count].size=sz;     sym_table[sym_count].kind=kind;
    return sym_count++;
}

/* ================================================================== */
/*  Procedure Table Operations  (NEW)                                   */
/* ================================================================== */
static int proc_find(const char *n){
    char up[MAX_TOKEN]; strncpy(up,n,MAX_TOKEN-1); str_toupper(up);
    for(int i=0;i<proc_count;i++)
        if(strcmp(proc_table[i].name,up)==0) return i;
    return -1;
}

/* Open a new procedure record */
static int proc_open(const char *n, SegID seg, int off, ProcType pt, int ln){
    char up[MAX_TOKEN]; strncpy(up,n,MAX_TOKEN-1); str_toupper(up);
    if(proc_find(up)!=-1){ add_error(ln,"Duplicate procedure '%s'",up); return -1; }
    if(proc_count>=MAX_PROCS){ add_error(ln,"Procedure table overflow"); return -1; }
    int i=proc_count++;
    memset(&proc_table[i],0,sizeof(ProcEntry));
    strncpy(proc_table[i].name,up,MAX_TOKEN-1);
    proc_table[i].segment     = seg;
    proc_table[i].start_offset= off;
    proc_table[i].end_offset  = UNDEF_ADDR;
    proc_table[i].type        = pt;
    proc_table[i].call_count  = 0;
    proc_table[i].has_frame   = 0;
    proc_table[i].frame_size  = 0;
    proc_table[i].local_count = 0;
    /* Also add to symbol table as a PROC symbol */
    sym_add(up, seg, off, 0, SYM_PROC, ln);
    return i;
}

/* Close an existing procedure record */
static void proc_close(int idx, int end_off){ proc_table[idx].end_offset=end_off; }

/* Add a local variable to current procedure */
static int proc_add_local(int pidx, const char *n, int sz, int ln){
    if(pidx<0){ add_error(ln,"LOCAL outside procedure"); return 0; }
    ProcEntry *pe=&proc_table[pidx];
    if(pe->local_count>=MAX_LOCALS){ add_error(ln,"Too many locals in '%s'",pe->name); return 0; }
    char up[MAX_TOKEN]; strncpy(up,n,MAX_TOKEN-1); str_toupper(up);
    pe->frame_size += sz;
    int bp_off = -(pe->frame_size);   /* [BP-2], [BP-4], … */
    int li=pe->local_count++;
    strncpy(pe->locals[li].name,up,MAX_TOKEN-1);
    pe->locals[li].bp_offset=bp_off;
    pe->locals[li].size=sz;
    return bp_off;
}

/* Resolve a local variable name → BP offset (or UNDEF_ADDR) */
static int proc_find_local(int pidx, const char *n){
    if(pidx<0) return UNDEF_ADDR;
    char up[MAX_TOKEN]; strncpy(up,n,MAX_TOKEN-1); str_toupper(up);
    ProcEntry *pe=&proc_table[pidx];
    for(int i=0;i<pe->local_count;i++)
        if(strcmp(pe->locals[i].name,up)==0) return pe->locals[i].bp_offset;
    return UNDEF_ADDR;
}

/* ================================================================== */
/*  Operand descriptor                                                  */
/* ================================================================== */
typedef enum { OT_NONE, OT_REG, OT_IMM, OT_MEM, OT_REGIND, OT_BPREL } OpType;
/*  OT_BPREL = [BP+/-offset]  used for local variables  */

typedef struct {
    OpType type;
    int    reg_code;
    int    is8bit;
    int    isseg;
    int    value;
    char   sym[MAX_TOKEN];
    int    sym_unresolved;
    int    bp_disp;   /* for OT_BPREL */
} Operand;

static Operand parse_operand(const char *tok, int line_no){
    Operand op; memset(&op,0,sizeof(op)); op.type=OT_NONE;
    if(!tok||!tok[0]) return op;

    char buf[MAX_TOKEN]; strncpy(buf,tok,MAX_TOKEN-1);
    char *t=trim(buf);
    char up[MAX_TOKEN]; strncpy(up,t,MAX_TOKEN-1); str_toupper(up);

    /* Register? */
    const RegEntry *re=find_reg(up);
    if(re){ op.type=OT_REG; op.reg_code=re->code; op.is8bit=re->is8bit; op.isseg=re->isseg; return op; }

    /* [xxx] */
    if(t[0]=='['){
        int len=(int)strlen(t);
        char inner[MAX_TOKEN];
        if(len>=2 && t[len-1]==']'){
            strncpy(inner,t+1,(size_t)(len-2)); inner[len-2]='\0'; trim(inner);
        } else { strncpy(inner,t+1,MAX_TOKEN-1); }
        char iup[MAX_TOKEN]; strncpy(iup,inner,MAX_TOKEN-1); str_toupper(iup);

        /* [BP+n] or [BP-n]  → OT_BPREL */
        if(strncmp(iup,"BP",2)==0 && (iup[2]=='+'||iup[2]=='-')){
            op.type=OT_BPREL;
            op.bp_disp=(int)strtol(inner+2,NULL,0);
            return op;
        }
        /* [BP] alone → OT_BPREL with disp=0 */
        if(strcmp(iup,"BP")==0){ op.type=OT_BPREL; op.bp_disp=0; return op; }

        /* Register indirect? */
        re=find_reg(iup);
        if(re){ op.type=OT_REGIND; op.reg_code=re->code; return op; }

        /* Direct memory */
        op.type=OT_MEM;
        char *end; long val=strtol(inner,&end,0);
        if(*end=='\0'){ op.value=(int)val; }
        else{
            char su[MAX_TOKEN]; strncpy(su,inner,MAX_TOKEN-1); str_toupper(su);
            int idx=sym_find(su);
            if(idx==-1){ op.sym_unresolved=1; strncpy(op.sym,su,MAX_TOKEN-1); add_error(line_no,"Undefined symbol '%s'",su); }
            else{ op.value=sym_table[idx].offset; strncpy(op.sym,su,MAX_TOKEN-1); }
        }
        return op;
    }

    /* Immediate: digit / '-' / 0x */
    if(isdigit((unsigned char)t[0])||t[0]=='-'||(t[0]=='0'&&(t[1]=='x'||t[1]=='X'))){
        op.type=OT_IMM; op.value=(int)strtol(t,NULL,0); return op;
    }

    /* Hex nH */
    { int len=(int)strlen(t);
      if(len>1 && toupper((unsigned char)t[len-1])=='H'){
          char hb[MAX_TOKEN]; strncpy(hb,t,(size_t)(len-1)); hb[len-1]='\0';
          char *end; long v=strtol(hb,&end,16);
          if(*end=='\0'){ op.type=OT_IMM; op.value=(int)v; return op; }
      }
    }

    /* Check local variable in current procedure */
    if(cur_proc_idx>=0){
        int bpoff=proc_find_local(cur_proc_idx,up);
        if(bpoff!=UNDEF_ADDR){ op.type=OT_BPREL; op.bp_disp=bpoff; return op; }
    }

    /* Label / symbol */
    { int idx=sym_find(up);
      op.type=OT_MEM;
      if(idx==-1){ op.sym_unresolved=1; strncpy(op.sym,up,MAX_TOKEN-1); }
      else{ op.value=sym_table[idx].offset; strncpy(op.sym,up,MAX_TOKEN-1); }
    }
    return op;
}

/* ================================================================== */
/*  Byte / word emitters                                                */
/* ================================================================== */
static void emit_byte(SegID seg, int *lc, unsigned char b, ListEntry *le){
    int flat=(seg==SEG_DATA)?(obj_data_start+*lc):(obj_code_start+*lc);
    if(flat>=0&&flat<MAX_OBJ_BYTES) obj_buf[flat]=b;
    if(le&&le->byte_count<16) le->bytes[le->byte_count++]=b;
    (*lc)++;
}
static void emit_word(SegID seg, int *lc, unsigned short w, ListEntry *le){
    emit_byte(seg,lc,(unsigned char)(w&0xFF),le);
    emit_byte(seg,lc,(unsigned char)((w>>8)&0xFF),le);
}

/* ================================================================== */
/*  ModRM helper for [BP+disp]                                          */
/* ================================================================== */
/* Emit  opcode  ModRM([BP+disp8])  disp8  */
static void emit_bprel_modrm(int *lc, unsigned char opcode,
                              unsigned char reg_field, int disp, ListEntry *le){
    emit_byte(SEG_CODE,lc,opcode,le);
    /* ModRM: mod=01 (disp8), reg=reg_field, rm=6 (BP+disp) */
    emit_byte(SEG_CODE,lc,(unsigned char)(0x46|(reg_field<<3)),le);
    emit_byte(SEG_CODE,lc,(unsigned char)(disp&0xFF),le);
}

/* ================================================================== */
/*  Instruction Encoder                                                 */
/* ================================================================== */
static int encode_instruction(const char *mnemonic,
                               const char *op1_str,
                               const char *op2_str,
                               int line_no, ListEntry *le)
{
    char mn[MAX_TOKEN]; strncpy(mn,mnemonic,MAX_TOKEN-1); str_toupper(mn);
    Operand op1,op2;
    memset(&op1,0,sizeof(op1)); op1.type=OT_NONE;
    memset(&op2,0,sizeof(op2)); op2.type=OT_NONE;
    if(op1_str&&op1_str[0]) op1=parse_operand(op1_str,line_no);
    if(op2_str&&op2_str[0]) op2=parse_operand(op2_str,line_no);
    int s=lc_code;

    /* --- NOP / HLT --- */
    if(!strcmp(mn,"NOP")){ emit_byte(SEG_CODE,&lc_code,0x90,le); return lc_code-s; }
    if(!strcmp(mn,"HLT")){ emit_byte(SEG_CODE,&lc_code,0xF4,le); return lc_code-s; }

    /* --- RET / RETF --- */
    if(!strcmp(mn,"RET")){
        if(op1.type==OT_IMM){
            emit_byte(SEG_CODE,&lc_code,0xC2,le);
            emit_word(SEG_CODE,&lc_code,(unsigned short)op1.value,le);
        } else { emit_byte(SEG_CODE,&lc_code,0xC3,le); }
        return lc_code-s;
    }
    if(!strcmp(mn,"RETF")){
        if(op1.type==OT_IMM){
            emit_byte(SEG_CODE,&lc_code,0xCA,le);
            emit_word(SEG_CODE,&lc_code,(unsigned short)op1.value,le);
        } else { emit_byte(SEG_CODE,&lc_code,0xCB,le); }
        return lc_code-s;
    }

    /* --- LEAVE (8086 equiv: MOV SP,BP / POP BP) --- */
    if(!strcmp(mn,"LEAVE")){
        emit_byte(SEG_CODE,&lc_code,0x89,le); /* MOV SP,BP */
        emit_byte(SEG_CODE,&lc_code,0xE5,le);
        emit_byte(SEG_CODE,&lc_code,0x5D,le); /* POP BP    */
        return lc_code-s;
    }

    /* --- ENTER framesize, nesting --- */
    if(!strcmp(mn,"ENTER")){
        /* Emit as: PUSH BP / MOV BP,SP / SUB SP,framesize */
        int fs=(op1.type==OT_IMM)?op1.value:0;
        emit_byte(SEG_CODE,&lc_code,0x55,le); /* PUSH BP */
        emit_byte(SEG_CODE,&lc_code,0x89,le); /* MOV BP,SP */
        emit_byte(SEG_CODE,&lc_code,0xEC,le);
        if(fs>0){
            if(fs<=127){
                emit_byte(SEG_CODE,&lc_code,0x83,le); /* SUB SP,imm8 */
                emit_byte(SEG_CODE,&lc_code,0xEC,le);
                emit_byte(SEG_CODE,&lc_code,(unsigned char)fs,le);
            } else {
                emit_byte(SEG_CODE,&lc_code,0x81,le); /* SUB SP,imm16 */
                emit_byte(SEG_CODE,&lc_code,0xEC,le);
                emit_word(SEG_CODE,&lc_code,(unsigned short)fs,le);
            }
        }
        return lc_code-s;
    }

    /* --- INT --- */
    if(!strcmp(mn,"INT")){
        emit_byte(SEG_CODE,&lc_code,0xCD,le);
        emit_byte(SEG_CODE,&lc_code,(unsigned char)(op1.value&0xFF),le);
        return lc_code-s;
    }

    /* --- PUSH / POP --- */
    if(!strcmp(mn,"PUSH")){
        if(op1.type==OT_REG&&!op1.is8bit&&!op1.isseg)
            emit_byte(SEG_CODE,&lc_code,(unsigned char)(0x50+op1.reg_code),le);
        else if(op1.type==OT_REG&&op1.isseg)
            emit_byte(SEG_CODE,&lc_code,(unsigned char)(0x06+(op1.reg_code<<3)),le);
        else if(op1.type==OT_IMM){
            emit_byte(SEG_CODE,&lc_code,0x68,le);
            emit_word(SEG_CODE,&lc_code,(unsigned short)op1.value,le);
        } else { add_error(line_no,"PUSH: unsupported operand"); }
        return lc_code-s;
    }
    if(!strcmp(mn,"POP")){
        if(op1.type==OT_REG&&!op1.is8bit&&!op1.isseg)
            emit_byte(SEG_CODE,&lc_code,(unsigned char)(0x58+op1.reg_code),le);
        else if(op1.type==OT_REG&&op1.isseg)
            emit_byte(SEG_CODE,&lc_code,(unsigned char)(0x07+(op1.reg_code<<3)),le);
        else { add_error(line_no,"POP: unsupported operand"); }
        return lc_code-s;
    }

    /* --- MOV --- */
    if(!strcmp(mn,"MOV")){
        /* MOV reg, [BP+disp] (local variable load) */
        if(op1.type==OT_REG&&!op1.is8bit&&op2.type==OT_BPREL){
            emit_bprel_modrm(&lc_code,0x8B,(unsigned char)op1.reg_code,op2.bp_disp,le);
            return lc_code-s;
        }
        /* MOV [BP+disp], reg (local variable store) */
        if(op1.type==OT_BPREL&&op2.type==OT_REG&&!op2.is8bit){
            emit_bprel_modrm(&lc_code,0x89,(unsigned char)op2.reg_code,op1.bp_disp,le);
            return lc_code-s;
        }
        /* MOV reg, imm */
        if(op1.type==OT_REG&&op2.type==OT_IMM){
            if(op1.is8bit){
                emit_byte(SEG_CODE,&lc_code,(unsigned char)(0xB0+op1.reg_code),le);
                emit_byte(SEG_CODE,&lc_code,(unsigned char)(op2.value&0xFF),le);
            } else {
                emit_byte(SEG_CODE,&lc_code,(unsigned char)(0xB8+op1.reg_code),le);
                emit_word(SEG_CODE,&lc_code,(unsigned short)op2.value,le);
            }
        }
        else if(op1.type==OT_REG&&op2.type==OT_REG&&!op1.is8bit&&!op2.is8bit){
            emit_byte(SEG_CODE,&lc_code,0x89,le);
            emit_byte(SEG_CODE,&lc_code,(unsigned char)(0xC0|op1.reg_code|(op2.reg_code<<3)),le);
        }
        else if(op1.type==OT_REG&&op2.type==OT_MEM&&!op1.is8bit){
            emit_byte(SEG_CODE,&lc_code,0x8B,le);
            emit_byte(SEG_CODE,&lc_code,(unsigned char)(0x06|(op1.reg_code<<3)),le);
            emit_word(SEG_CODE,&lc_code,(unsigned short)op2.value,le);
        }
        else if(op1.type==OT_MEM&&op2.type==OT_REG&&!op2.is8bit){
            emit_byte(SEG_CODE,&lc_code,0x89,le);
            emit_byte(SEG_CODE,&lc_code,(unsigned char)(0x06|(op2.reg_code<<3)),le);
            emit_word(SEG_CODE,&lc_code,(unsigned short)op1.value,le);
        }
        else if(op1.type==OT_REG&&op2.type==OT_REGIND){
            emit_byte(SEG_CODE,&lc_code,0x8B,le);
            emit_byte(SEG_CODE,&lc_code,(unsigned char)((op1.reg_code<<3)|op2.reg_code),le);
        }
        else if(op1.type==OT_REGIND&&op2.type==OT_REG){
            emit_byte(SEG_CODE,&lc_code,0x89,le);
            emit_byte(SEG_CODE,&lc_code,(unsigned char)((op2.reg_code<<3)|op1.reg_code),le);
        }
        else if(op1.type==OT_REG&&op1.isseg&&op2.type==OT_REG){
            emit_byte(SEG_CODE,&lc_code,0x8E,le);
            emit_byte(SEG_CODE,&lc_code,(unsigned char)(0xC0|(op1.reg_code<<3)|op2.reg_code),le);
        }
        else { add_error(line_no,"MOV: unsupported operand combination"); }
        return lc_code-s;
    }

    /* --- ALU: ADD SUB AND OR XOR CMP --- */
    { typedef struct{const char*mn;unsigned char rr,rf;}A;
      static const A al[]={{"ADD",0x01,0},{"SUB",0x29,5},{"AND",0x21,4},
                            {"OR", 0x09,1},{"XOR",0x31,6},{"CMP",0x39,7},{NULL,0,0}};
      for(int i=0;al[i].mn;i++) if(!strcmp(mn,al[i].mn)){
        if(op1.type==OT_REG&&op2.type==OT_REG){
            emit_byte(SEG_CODE,&lc_code,al[i].rr,le);
            emit_byte(SEG_CODE,&lc_code,(unsigned char)(0xC0|(op2.reg_code<<3)|op1.reg_code),le);
        } else if(op1.type==OT_REG&&op2.type==OT_IMM){
            int imm=op2.value;
            if(imm>=-128&&imm<=127){
                emit_byte(SEG_CODE,&lc_code,0x83,le);
                emit_byte(SEG_CODE,&lc_code,(unsigned char)(0xC0|(al[i].rf<<3)|op1.reg_code),le);
                emit_byte(SEG_CODE,&lc_code,(unsigned char)(imm&0xFF),le);
            } else {
                emit_byte(SEG_CODE,&lc_code,0x81,le);
                emit_byte(SEG_CODE,&lc_code,(unsigned char)(0xC0|(al[i].rf<<3)|op1.reg_code),le);
                emit_word(SEG_CODE,&lc_code,(unsigned short)imm,le);
            }
        } else if(op1.type==OT_REG&&op2.type==OT_MEM){
            emit_byte(SEG_CODE,&lc_code,(unsigned char)(al[i].rr-1),le);
            emit_byte(SEG_CODE,&lc_code,(unsigned char)(0x06|(op1.reg_code<<3)),le);
            emit_word(SEG_CODE,&lc_code,(unsigned short)op2.value,le);
        } else if(op1.type==OT_REG&&op2.type==OT_BPREL){
            /* ADD reg, [BP+off] */
            unsigned char base=(unsigned char)(al[i].rr-1);
            emit_bprel_modrm(&lc_code,base,(unsigned char)op1.reg_code,op2.bp_disp,le);
        } else { add_error(line_no,"%s: unsupported operands",mn); }
        return lc_code-s;
      }
    }

    /* --- INC / DEC --- */
    if(!strcmp(mn,"INC")||!strcmp(mn,"DEC")){
        int base=(!strcmp(mn,"INC"))?0x40:0x48;
        if(op1.type==OT_REG&&!op1.is8bit)
            emit_byte(SEG_CODE,&lc_code,(unsigned char)(base+op1.reg_code),le);
        else { add_error(line_no,"%s: unsupported operand",mn); }
        return lc_code-s;
    }

    /* --- MUL / DIV --- */
    if(!strcmp(mn,"MUL")||!strcmp(mn,"DIV")){
        unsigned char rf=(!strcmp(mn,"MUL"))?4:6;
        if(op1.type==OT_REG&&!op1.is8bit){
            emit_byte(SEG_CODE,&lc_code,0xF7,le);
            emit_byte(SEG_CODE,&lc_code,(unsigned char)(0xC0|(rf<<3)|op1.reg_code),le);
        } else { add_error(line_no,"%s: unsupported operand",mn); }
        return lc_code-s;
    }

    /* --- NOT / NEG --- */
    if(!strcmp(mn,"NOT")||!strcmp(mn,"NEG")){
        unsigned char rf=(!strcmp(mn,"NOT"))?2:3;
        if(op1.type==OT_REG&&!op1.is8bit){
            emit_byte(SEG_CODE,&lc_code,0xF7,le);
            emit_byte(SEG_CODE,&lc_code,(unsigned char)(0xC0|(rf<<3)|op1.reg_code),le);
        } else { add_error(line_no,"%s: unsupported operand",mn); }
        return lc_code-s;
    }

    /* --- SHL / SHR --- */
    if(!strcmp(mn,"SHL")||!strcmp(mn,"SHR")){
        unsigned char rf=(!strcmp(mn,"SHL"))?4:5;
        if(op1.type==OT_REG&&op2.type==OT_IMM){
            if(op2.value==1){ emit_byte(SEG_CODE,&lc_code,0xD1,le); emit_byte(SEG_CODE,&lc_code,(unsigned char)(0xC0|(rf<<3)|op1.reg_code),le); }
            else            { emit_byte(SEG_CODE,&lc_code,0xC1,le); emit_byte(SEG_CODE,&lc_code,(unsigned char)(0xC0|(rf<<3)|op1.reg_code),le); emit_byte(SEG_CODE,&lc_code,(unsigned char)(op2.value&0xFF),le); }
        } else { add_error(line_no,"%s: unsupported operands",mn); }
        return lc_code-s;
    }

    /* --- CALL --- */
    if(!strcmp(mn,"CALL")){
        /* FAR CALL: op1_str starts with "FAR" */
        int far_call=0;
        const char *target_str=op1_str;
        if(op1_str){
            char tmp[MAX_TOKEN]; strncpy(tmp,op1_str,MAX_TOKEN-1); str_toupper(tmp);
            if(strncmp(tmp,"FAR",3)==0){ far_call=1; target_str=ltrim((char*)op1_str+3); }
        }
        int target=0;
        if(!far_call){
            /* Check if it's a known procedure → count calls */
            if(target_str){
                char tu[MAX_TOKEN]; strncpy(tu,target_str,MAX_TOKEN-1); str_toupper(tu);
                int pi=proc_find(tu);
                if(pi>=0) proc_table[pi].call_count++;
                /* Resolve address */
                int si=sym_find(tu);
                if(si>=0) target=sym_table[si].offset;
            }
            emit_byte(SEG_CODE,&lc_code,0xE8,le);
            int rel=target-(lc_code+2);
            emit_word(SEG_CODE,&lc_code,(unsigned short)rel,le);
        } else {
            /* Far call: emit CALL FAR PTR (simplified: 0x9A + offset + segment=0) */
            emit_byte(SEG_CODE,&lc_code,0x9A,le);
            emit_word(SEG_CODE,&lc_code,(unsigned short)target,le);
            emit_word(SEG_CODE,&lc_code,0x0000,le); /* segment placeholder */
        }
        return lc_code-s;
    }

    /* --- Jumps --- */
    { typedef struct{const char*mn;unsigned char op;}J;
      static const J jt[]={
          {"JMP",0xE9},{"JE",0x74},{"JNE",0x75},{"JZ",0x74},{"JNZ",0x75},
          {"JL",0x7C},{"JG",0x7F},{"JLE",0x7E},{"JGE",0x7D},
          {"JA",0x77},{"JB",0x72},{"JC",0x72},{"JNC",0x73},
          {"JS",0x78},{"JNS",0x79},{"JO",0x70},{"JNO",0x71},
          {NULL,0}};
      for(int i=0;jt[i].mn;i++) if(!strcmp(mn,jt[i].mn)){
          int target=0;
          if(op1.type==OT_MEM||op1.type==OT_IMM) target=op1.value;
          if(jt[i].op==0xE9){
              emit_byte(SEG_CODE,&lc_code,0xE9,le);
              int rel=target-(lc_code+2);
              emit_word(SEG_CODE,&lc_code,(unsigned short)rel,le);
          } else {
              emit_byte(SEG_CODE,&lc_code,jt[i].op,le);
              int rel=target-(lc_code+1);
              emit_byte(SEG_CODE,&lc_code,(unsigned char)(rel&0xFF),le);
          }
          return lc_code-s;
      }
    }

    /* --- XCHG --- */
    if(!strcmp(mn,"XCHG")){
        if(op1.type==OT_REG&&op2.type==OT_REG&&!op1.is8bit&&!op2.is8bit){
            /* XCHG AX,rw → 0x90+rw; otherwise use 0x87 */
            if(op1.reg_code==0)
                emit_byte(SEG_CODE,&lc_code,(unsigned char)(0x90+op2.reg_code),le);
            else{
                emit_byte(SEG_CODE,&lc_code,0x87,le);
                emit_byte(SEG_CODE,&lc_code,(unsigned char)(0xC0|(op1.reg_code<<3)|op2.reg_code),le);
            }
        } else { add_error(line_no,"XCHG: unsupported operands"); }
        return lc_code-s;
    }

    add_error(line_no,"Unknown instruction '%s'",mn);
    return 0;
}

/* ================================================================== */
/*  Directive size helpers                                              */
/* ================================================================== */
static int directive_size(const char *dir, const char *operand){
    char up[MAX_TOKEN]; strncpy(up,dir,MAX_TOKEN-1); str_toupper(up);
    if(!strcmp(up,"DB")){
        int c=0; const char *p=operand; int in_s=0;
        while(*p){
            if(*p=='"'||*p=='\''){ in_s=!in_s; if(!in_s)c++; p++; continue; }
            if(in_s){c++;p++;continue;}
            if(*p==','){p++;continue;}
            while(*p&&*p!=','&&*p!='"'&&*p!='\'') p++;
            if(p>operand&&*(p-1)!=',') c++;
        }
        return c?c:1;
    }
    if(!strcmp(up,"DW")){
        const char *dup=strstr(operand,"DUP"); if(!dup)dup=strstr(operand,"dup");
        if(dup){ int n=(int)strtol(operand,NULL,10); return n*2; }
        int cm=0; for(const char*q=operand;*q;q++) if(*q==',')cm++;
        return (cm+1)*2;
    }
    if(!strcmp(up,"DD")){
        int cm=0; for(const char*q=operand;*q;q++) if(*q==',')cm++;
        return (cm+1)*4;
    }
    return 0;
}

static void emit_data_directive(const char *dir, const char *operand, int line_no, ListEntry *le){
    char up[MAX_TOKEN]; strncpy(up,dir,MAX_TOKEN-1); str_toupper(up);
    if(!strcmp(up,"DB")){
        const char *p=operand;
        while(*p){ p=ltrim((char*)p);
            if(*p=='"'||*p=='\''){
                char d=*p++;
                while(*p&&*p!=d){ emit_byte(SEG_DATA,&lc_data,(unsigned char)*p,le); p++; }
                if(*p)p++;
            } else {
                char nb[MAX_TOKEN]; int ni=0;
                while(*p&&*p!=','&&!isspace((unsigned char)*p)) nb[ni++]=*p++;
                nb[ni]='\0';
                if(ni>0) emit_byte(SEG_DATA,&lc_data,(unsigned char)(strtol(nb,NULL,0)&0xFF),le);
            }
            p=ltrim((char*)p); if(*p==',')p++;
        } return;
    }
    if(!strcmp(up,"DW")){
        const char *dup=strstr(operand,"DUP"); if(!dup)dup=strstr(operand,"dup");
        if(dup){
            int n=(int)strtol(operand,NULL,10);
            const char *ip=strchr(dup,'('); int v=0;
            if(ip)v=(int)strtol(ip+1,NULL,0);
            for(int k=0;k<n;k++) emit_word(SEG_DATA,&lc_data,(unsigned short)v,le);
            return;
        }
        const char *p=operand;
        while(*p){ p=ltrim((char*)p);
            if(*p=='?'){ emit_word(SEG_DATA,&lc_data,0,le); p++; }
            else{ char nb[MAX_TOKEN]; int ni=0; while(*p&&*p!=',')nb[ni++]=*p++; nb[ni]='\0'; trim(nb);
                if(ni>0) emit_word(SEG_DATA,&lc_data,(unsigned short)(strtol(nb,NULL,0)&0xFFFF),le); }
            p=ltrim((char*)p); if(*p==',')p++;
        } return;
    }
    if(!strcmp(up,"DD")){
        const char *p=operand;
        while(*p){ p=ltrim((char*)p);
            char nb[MAX_TOKEN]; int ni=0; while(*p&&*p!=',')nb[ni++]=*p++; nb[ni]='\0'; trim(nb);
            if(ni>0){ unsigned long v=strtoul(nb,NULL,0);
                emit_byte(SEG_DATA,&lc_data,(unsigned char)(v&0xFF),le);
                emit_byte(SEG_DATA,&lc_data,(unsigned char)((v>>8)&0xFF),le);
                emit_byte(SEG_DATA,&lc_data,(unsigned char)((v>>16)&0xFF),le);
                emit_byte(SEG_DATA,&lc_data,(unsigned char)((v>>24)&0xFF),le); }
            p=ltrim((char*)p); if(*p==',')p++;
        } return;
    }
    (void)line_no;
}

/* ================================================================== */
/*  Tokeniser                                                           */
/* ================================================================== */
static void tokenise(const char *line,
                     char *label, char *mnemonic, char *op1, char *op2)
{
    label[0]=mnemonic[0]=op1[0]=op2[0]='\0';
    char buf[MAX_LINE]; strncpy(buf,line,MAX_LINE-1);
    char *semi=strchr(buf,';'); if(semi)*semi='\0';
    char *p=trim(buf); if(!p||!*p) return;

    /* Colon-label */
    char *colon=strchr(p,':');
    if(colon){
        int ll=(int)(colon-p);
        if(ll>0){ strncpy(label,p,(size_t)ll); label[ll]='\0'; trim(label); str_toupper(label); }
        p=ltrim(colon+1);
    } else {
        /* label before PROC/ENDP/EQU/DB/DW/DD */
        char f[MAX_TOKEN]="",sc[MAX_TOKEN]="";
        char tmp[MAX_LINE]; strncpy(tmp,p,MAX_LINE-1);
        char *t1=strtok(tmp," \t"); if(t1)strncpy(f,t1,MAX_TOKEN-1);
        char *t2=strtok(NULL," \t"); if(t2)strncpy(sc,t2,MAX_TOKEN-1);
        char s2[MAX_TOKEN]; strncpy(s2,sc,MAX_TOKEN-1); str_toupper(s2);
        if(sc[0]&&(strcmp(s2,"PROC")==0||strcmp(s2,"ENDP")==0||
                   strcmp(s2,"EQU")==0||strcmp(s2,"DB")==0||
                   strcmp(s2,"DW")==0||strcmp(s2,"DD")==0)){
            strncpy(label,f,MAX_TOKEN-1); str_toupper(label);
            while(*p&&!isspace((unsigned char)*p))p++; p=ltrim(p);
        }
    }

    /* Mnemonic */
    char *sp=p;
    while(*sp&&!isspace((unsigned char)*sp))sp++;
    int ml=(int)(sp-p);
    if(ml>0){ strncpy(mnemonic,p,(size_t)ml); mnemonic[ml]='\0'; str_toupper(mnemonic); }
    p=ltrim(sp);
    if(!*p) return;

    /* Operands (split at first comma outside brackets) */
    int depth=0; const char *cm=NULL;
    for(const char*q=p;*q;q++){
        if(*q=='['||*q=='(')depth++;
        else if(*q==']'||*q==')')depth--;
        else if(*q==','&&depth==0){cm=q;break;}
    }
    if(cm){
        int l1=(int)(cm-p); strncpy(op1,p,(size_t)l1); op1[l1]='\0'; trim(op1);
        strncpy(op2,cm+1,MAX_TOKEN-1); trim(op2);
    } else { strncpy(op1,p,MAX_TOKEN-1); trim(op1); }
}

/* ================================================================== */
/*  Directive classification                                            */
/* ================================================================== */
static int is_data_dir(const char *m){
    return !strcmp(m,"DB")||!strcmp(m,"DW")||!strcmp(m,"DD");
}
static int is_pseudo(const char *m){
    return !strcmp(m,".MODEL")||!strcmp(m,".STACK")||!strcmp(m,".DATA")||
           !strcmp(m,".CODE")||!strcmp(m,"ENDS")||!strcmp(m,"END")||
           !strcmp(m,"ORG")||!strcmp(m,"EQU")||!strcmp(m,"ASSUME")||
           !strcmp(m,"PROC")||!strcmp(m,"ENDP")||!strcmp(m,"LOCAL")||
           is_data_dir(m);
}

static int est_size(const char *mn, const char *o1, const char *o2){
    if(!strcmp(mn,"NOP")||!strcmp(mn,"HLT")) return 1;
    if(!strcmp(mn,"RET")||!strcmp(mn,"RETF")) return (o1&&o1[0])?3:1;
    if(!strcmp(mn,"LEAVE")) return 3;
    if(!strcmp(mn,"ENTER")) return 6;
    if(!strcmp(mn,"INT"))   return 2;
    if(!strcmp(mn,"PUSH")||!strcmp(mn,"POP")) return 1;
    if(!strcmp(mn,"INC")||!strcmp(mn,"DEC")) return 2;
    if(!strcmp(mn,"MUL")||!strcmp(mn,"DIV")) return 2;
    if(!strcmp(mn,"NOT")||!strcmp(mn,"NEG")) return 2;
    if(!strncmp(mn,"J",1)||!strcmp(mn,"CALL")) return 3;
    if(!strcmp(mn,"MOV")){
        const RegEntry *r=find_reg(o1);
        if(r&&!r->is8bit&&(!find_reg(o2)))return 3;
        if(r&&r->is8bit) return 2;
        if(o1[0]=='['||o2[0]=='[') return 4;
        return 3;
    }
    if(o2&&(isdigit((unsigned char)o2[0])||o2[0]=='-'))return 4;
    return 3;
}

/* ================================================================== */
/*  Prologue / Epilogue emitters  (NEW)                                 */
/* ================================================================== */

/* Emit standard NEAR frame prologue: PUSH BP / MOV BP,SP [/ SUB SP,n] */
static void emit_prologue(int frame_sz, ListEntry *le){
    emit_byte(SEG_CODE,&lc_code,0x55,le);       /* PUSH BP    */
    emit_byte(SEG_CODE,&lc_code,0x89,le);       /* MOV BP,SP  */
    emit_byte(SEG_CODE,&lc_code,0xEC,le);
    if(frame_sz>0){
        if(frame_sz<=127){
            emit_byte(SEG_CODE,&lc_code,0x83,le); /* SUB SP,imm8 */
            emit_byte(SEG_CODE,&lc_code,0xEC,le);
            emit_byte(SEG_CODE,&lc_code,(unsigned char)frame_sz,le);
        } else {
            emit_byte(SEG_CODE,&lc_code,0x81,le); /* SUB SP,imm16 */
            emit_byte(SEG_CODE,&lc_code,0xEC,le);
            emit_word(SEG_CODE,&lc_code,(unsigned short)frame_sz,le);
        }
    }
}

/* Emit standard epilogue: MOV SP,BP / POP BP  (no RET – user writes RET) */
static void emit_epilogue(ListEntry *le){
    emit_byte(SEG_CODE,&lc_code,0x89,le); /* MOV SP,BP */
    emit_byte(SEG_CODE,&lc_code,0xE5,le);
    emit_byte(SEG_CODE,&lc_code,0x5D,le); /* POP  BP   */
}

/* ================================================================== */
/*  LOCAL directive handler  (NEW)                                      */
/* ================================================================== */
/* Parse "LOCAL varname:WORD, varname2:DWORD, ..." */
static void handle_local(const char *operand_str, int line_no){
    if(cur_proc_idx<0){ add_error(line_no,"LOCAL outside PROC"); return; }
    char buf[MAX_LINE]; strncpy(buf,operand_str,MAX_LINE-1);
    char *p=buf;
    while(*p){
        p=ltrim(p);
        /* get name */
        char vname[MAX_TOKEN]="";
        int vi=0;
        while(*p&&*p!=':'&&*p!=','&&!isspace((unsigned char)*p)) vname[vi++]=*p++;
        vname[vi]='\0';
        /* get type after ':' */
        int sz=2; /* default WORD */
        if(*p==':'){
            p++;
            char vtype[MAX_TOKEN]=""; int ti=0;
            while(*p&&*p!=','&&!isspace((unsigned char)*p)) vtype[ti++]=*p++;
            vtype[ti]='\0'; str_toupper(vtype);
            if(!strcmp(vtype,"BYTE"))  sz=1;
            else if(!strcmp(vtype,"DWORD")) sz=4;
            else sz=2; /* WORD default */
        }
        if(vname[0]) proc_add_local(cur_proc_idx,vname,sz,line_no);
        p=ltrim(p); if(*p==',')p++;
    }
}

/* ================================================================== */
/*  PASS 1                                                              */
/* ================================================================== */
static void pass1(void){
    cur_seg=SEG_NONE; lc_data=lc_code=lc_stack=0; cur_proc_idx=-1;

    for(int i=0;i<src_count;i++){
        char lb[MAX_TOKEN],mn[MAX_TOKEN],o1[MAX_TOKEN],o2[MAX_TOKEN];
        tokenise(src_lines[i].text,lb,mn,o1,o2);
        int ln=src_lines[i].line_no;
        if(!mn[0]&&!lb[0]) continue;

        if(!strcmp(mn,".DATA")) { cur_seg=SEG_DATA; continue; }
        if(!strcmp(mn,".CODE")) { cur_seg=SEG_CODE; continue; }
        if(!strcmp(mn,".STACK")){ cur_seg=SEG_STACK; obj_stack_size=o1[0]?(int)strtol(o1,NULL,0):256; continue; }
        if(!strcmp(mn,".MODEL")||!strcmp(mn,"ASSUME")||!strcmp(mn,"ENDS")) continue;
        if(!strcmp(mn,"END")) break;
        if(!strcmp(mn,"ORG")){ int v=(int)strtol(o1,NULL,0); if(cur_seg==SEG_DATA)lc_data=v; else if(cur_seg==SEG_CODE)lc_code=v; continue; }
        if(!strcmp(mn,"EQU")){ if(lb[0])sym_add(lb,SEG_NONE,(int)strtol(o1,NULL,0),0,SYM_EQU,ln); continue; }
        /* LOCAL (raw-line detection to avoid colon-in-type tokeniser issue) */
        {
            char raw_up[MAX_LINE]; strncpy(raw_up,src_lines[i].text,MAX_LINE-1);
            str_toupper(raw_up);
            char *lpos=strstr(raw_up,"LOCAL");
            if(lpos && ((lpos==raw_up)||(*(lpos-1)==' ')||(*(lpos-1)=='\t'))){
                char raw_orig[MAX_LINE]; strncpy(raw_orig,src_lines[i].text,MAX_LINE-1);
                char *lo=strstr(raw_orig,"LOCAL"); if(!lo) lo=strstr(raw_orig,"local");
                if(lo){ lo+=5; handle_local(ltrim(lo),ln); }
                continue;
            }
        }

        /* PROC */
        if(!strcmp(mn,"PROC")){
            if(!lb[0]){ add_error(ln,"PROC without name"); continue; }
            ProcType pt=PROC_NEAR;
            char ou[MAX_TOKEN]; strncpy(ou,o1,MAX_TOKEN-1); str_toupper(ou);
            if(!strcmp(ou,"FAR")) pt=PROC_FAR;
            cur_proc_idx=proc_open(lb,cur_seg,lc_code,pt,ln);
            continue;
        }
        /* ENDP */
        if(!strcmp(mn,"ENDP")){
            if(cur_proc_idx>=0){ proc_close(cur_proc_idx,lc_code); cur_proc_idx=-1; }
            else add_error(ln,"ENDP without matching PROC");
            continue;
        }

        /* Label definition */
        if(lb[0]){
            SegID lseg=cur_seg;
            int   loff=(cur_seg==SEG_DATA)?lc_data:(cur_seg==SEG_CODE?lc_code:0);
            int   sz=is_data_dir(mn)?directive_size(mn,o1):0;
            SymKind kd=is_data_dir(mn)?SYM_VAR:SYM_LABEL;
            sym_add(lb,lseg,loff,sz,kd,ln);
        }

        /* Advance LC */
        if(mn[0]){
            if(is_data_dir(mn)){ if(cur_seg==SEG_DATA) lc_data+=directive_size(mn,o1); }
            else if(!is_pseudo(mn))  { if(cur_seg==SEG_CODE) lc_code+=est_size(mn,o1,o2); }
        }
    }

    obj_data_start=obj_stack_size;
    obj_code_start=obj_stack_size+lc_data;
}

/* ================================================================== */
/*  PASS 2                                                              */
/* ================================================================== */
static void pass2(void){
    cur_seg=SEG_NONE; lc_data=lc_code=0; cur_proc_idx=-1;

    for(int i=0;i<src_count;i++){
        char lb[MAX_TOKEN],mn[MAX_TOKEN],o1[MAX_TOKEN],o2[MAX_TOKEN];
        char src_copy[MAX_LINE]; strncpy(src_copy,src_lines[i].text,MAX_LINE-1); trim(src_copy);
        tokenise(src_lines[i].text,lb,mn,o1,o2);
        int ln=src_lines[i].line_no;

        ListEntry le; memset(&le,0,sizeof(le));
        le.line_no=ln; le.seg=cur_seg; le.seg_offset=-1;
        strncpy(le.source,src_copy[0]?src_copy:"(blank)",MAX_LINE-1);

        if(!mn[0]&&!lb[0]){ list_table[list_count++]=le; continue; }

        if(!strcmp(mn,".DATA")) { cur_seg=SEG_DATA;  le.seg=SEG_DATA;  list_table[list_count++]=le; continue; }
        if(!strcmp(mn,".CODE")) { cur_seg=SEG_CODE;  le.seg=SEG_CODE;  list_table[list_count++]=le; continue; }
        if(!strcmp(mn,".STACK")){ cur_seg=SEG_STACK; le.seg=SEG_STACK; list_table[list_count++]=le; continue; }
        if(!strcmp(mn,".MODEL")||!strcmp(mn,"ASSUME")||!strcmp(mn,"ENDS")){ list_table[list_count++]=le; continue; }
        if(!strcmp(mn,"END"))   { list_table[list_count++]=le; break; }
        if(!strcmp(mn,"ORG"))   { int v=(int)strtol(o1,NULL,0); if(cur_seg==SEG_DATA)lc_data=v; else if(cur_seg==SEG_CODE)lc_code=v; list_table[list_count++]=le; continue; }
        if(!strcmp(mn,"EQU"))   { list_table[list_count++]=le; continue; }

        le.seg=cur_seg;

        /* ---- PROC (NEW: emit prologue) ---- */
        if(!strcmp(mn,"PROC")){
            if(lb[0]){
                char ou[MAX_TOKEN]; strncpy(ou,o1,MAX_TOKEN-1); str_toupper(ou);
                int pi=proc_find(lb);
                if(pi>=0){
                    cur_proc_idx=pi;
                    snprintf(le.proc_marker,MAX_TOKEN,"PROC %s (%s)",
                             lb,proc_table[pi].type==PROC_FAR?"FAR":"NEAR");
                    le.seg_offset=lc_code;
                    /* Emit standard frame prologue */
                    emit_prologue(proc_table[pi].frame_size,&le);
                    proc_table[pi].has_frame=1;
                    proc_table[pi].start_offset=lc_code-(le.byte_count); /* adjust for prologue */
                }
            }
            list_table[list_count++]=le; continue;
        }

        /* ---- ENDP (NEW: emit epilogue) ---- */
        if(!strcmp(mn,"ENDP")){
            if(cur_proc_idx>=0){
                snprintf(le.proc_marker,MAX_TOKEN,"ENDP %s",proc_table[cur_proc_idx].name);
                le.seg_offset=lc_code;
                emit_epilogue(&le);
                proc_table[cur_proc_idx].end_offset=lc_code;
                cur_proc_idx=-1;
            }
            list_table[list_count++]=le; continue;
        }

        /* ---- LOCAL (raw-line detection to avoid colon-in-type tokeniser issue) ---- */
        {
            char raw_up[MAX_LINE]; strncpy(raw_up,src_lines[i].text,MAX_LINE-1);
            str_toupper(raw_up);
            char *lpos=strstr(raw_up,"LOCAL");
            if(lpos && ((lpos==raw_up)||(*(lpos-1)==' ')||(*(lpos-1)=='\t'))){
                /* Pass2: locals already registered in pass1 – just record listing */
                list_table[list_count++]=le; continue;
            }
        }

        /* ---- DATA directives ---- */
        if(cur_seg==SEG_DATA && is_data_dir(mn)){
            le.seg_offset=lc_data;
            emit_data_directive(mn,o1,ln,&le);
            list_table[list_count++]=le; continue;
        }

        /* ---- CODE instructions ---- */
        if(cur_seg==SEG_CODE && mn[0] && !is_pseudo(mn)){
            le.seg_offset=lc_code;
            encode_instruction(mn,o1[0]?o1:NULL,o2[0]?o2:NULL,ln,&le);
            list_table[list_count++]=le; continue;
        }

        list_table[list_count++]=le;
    }
}

/* ================================================================== */
/*  Object file writer                                                  */
/* ================================================================== */
static void write_obj(const char *fn){
    FILE *f=fopen(fn,"w"); if(!f){perror(fn);return;}
    fprintf(f,"; 8086 Object File – PR5 (Procedure-aware)\n");
    fprintf(f,"; Stack: 0x%04X bytes  Data base: 0x%04X  Code base: 0x%04X\n\n",
            obj_stack_size,obj_data_start,obj_code_start);
    for(int off=0;off<lc_data;off+=16){
        fprintf(f,"DATA  %04X  ",off);
        for(int k=off;k<off+16&&k<lc_data;k++) fprintf(f,"%02X ",obj_buf[obj_data_start+k]);
        fprintf(f,"\n");
    }
    for(int off=0;off<lc_code;off+=16){
        fprintf(f,"CODE  %04X  ",off);
        for(int k=off;k<off+16&&k<lc_code;k++) fprintf(f,"%02X ",obj_buf[obj_code_start+k]);
        fprintf(f,"\n");
    }
    fclose(f);
}

/* ================================================================== */
/*  Report printers                                                     */
/* ================================================================== */
static void sep(char c,int n){ for(int i=0;i<n;i++)putchar(c); putchar('\n'); }

static void print_symbol_table(void){
    printf("\n"); sep('=',72);
    printf("  SYMBOL TABLE\n"); sep('=',72);
    printf("  %-20s  %-7s  %-8s  %-6s  %s\n","Label","Seg","Offset","Size","Kind");
    sep('-',72);
    for(int i=0;i<sym_count;i++){
        Symbol *s=&sym_table[i];
        const char *ks=(s->kind==SYM_EQU)?"EQU":(s->kind==SYM_VAR)?"VAR":(s->kind==SYM_PROC)?"PROC":"LABEL";
        printf("  %-20s  %-7s  0x%04X    %-6d  %s\n",s->name,seg_name(s->segment),s->offset,s->size,ks);
    }
    sep('=',72);
}

static void print_proc_table(void){
    printf("\n"); sep('=',72);
    printf("  PROCEDURE TABLE\n"); sep('=',72);
    printf("  %-16s  %-5s  %-6s  %-6s  %-6s  %-5s  %s\n",
           "Name","Type","Start","End","Size","Calls","Locals");
    sep('-',72);
    if(proc_count==0){ printf("  (no procedures)\n"); }
    for(int i=0;i<proc_count;i++){
        ProcEntry *pe=&proc_table[i];
        int sz=(pe->end_offset>=0&&pe->end_offset>=pe->start_offset)?
               (pe->end_offset-pe->start_offset):(-1);
        printf("  %-16s  %-5s  0x%04X  0x%04X  %-6d  %-5d  %d\n",
               pe->name,
               pe->type==PROC_FAR?"FAR":"NEAR",
               pe->start_offset,
               pe->end_offset>=0?pe->end_offset:0,
               sz>=0?sz:0,
               pe->call_count,
               pe->local_count);
        /* Print locals */
        for(int j=0;j<pe->local_count;j++){
            printf("    [local] %-14s  BP%+d  (%d byte%s)\n",
                   pe->locals[j].name,
                   pe->locals[j].bp_offset,
                   pe->locals[j].size,
                   pe->locals[j].size==1?"":"s");
        }
    }
    sep('=',72);
}

static void print_error_table(void){
    printf("\n"); sep('=',72);
    printf("  ERROR TABLE\n"); sep('=',72);
    if(err_count==0){ printf("  No errors. Assembly successful.\n"); }
    else{
        printf("  %-6s  %s\n","Line","Message"); sep('-',72);
        for(int i=0;i<err_count;i++)
            printf("  %-6d  %s\n",err_table[i].line_no,err_table[i].message);
    }
    sep('=',72);
}

static void print_listing(void){
    printf("\n"); sep('=',72);
    printf("  OBJECT LISTING\n"); sep('=',72);
    printf("  %-4s  %-5s  %-4s  %-22s  %s\n","Line","Seg","Off.","Hex Bytes","Source");
    sep('-',72);
    for(int i=0;i<list_count;i++){
        ListEntry *le=&list_table[i];
        char hx[72]="";
        for(int k=0;k<le->byte_count&&k<8;k++){
            char t[4]; snprintf(t,4,"%02X ",le->bytes[k]);
            strncat(hx,t,sizeof(hx)-strlen(hx)-1);
        }
        if(le->proc_marker[0]){
            printf("  %-4d  %-5s  ----  %-22s  ;;; %s\n",
                   le->line_no,seg_name(le->seg),"",le->proc_marker);
        }
        if(le->byte_count>0){
            printf("  %-4d  %-5s  %04X  %-22s  %s\n",
                   le->line_no,seg_name(le->seg),le->seg_offset,hx,le->source);
        } else {
            printf("  %-4d  %-5s  ----  %-22s  %s\n",
                   le->line_no,seg_name(le->seg),"",le->source);
        }
    }
    sep('=',72);
}

/* ================================================================== */
/*  main                                                                */
/* ================================================================== */
int main(int argc,char *argv[]){
    if(argc<2){ fprintf(stderr,"Usage: %s <source.asm>\n",argv[0]); return 1; }
    FILE *fp=fopen(argv[1],"r"); if(!fp){perror(argv[1]);return 1;}
    char line[MAX_LINE]; int line_no=0;
    while(fgets(line,sizeof(line),fp)&&src_count<MAX_SOURCE_LINES){
        line_no++;
        line[strcspn(line,"\r\n")]='\0';
        src_lines[src_count].line_no=line_no;
        strncpy(src_lines[src_count].text,line,MAX_LINE-1);
        src_count++;
    }
    fclose(fp);

    sep('=',72);
    printf("  8086 Two-Pass Assembler with Procedure Support  –  PR5\n");
    printf("  Source : %s   (%d lines)\n",argv[1],src_count);
    sep('=',72);

    printf("\n[Pass 1] Building symbol & procedure tables...\n");
    pass1();
    printf("         %d symbol(s)  |  %d procedure(s)\n",sym_count,proc_count);
    printf("         Data: %d bytes  |  Code: %d bytes (est.)\n",lc_data,lc_code);

    printf("\n[Pass 2] Generating object code...\n");
    memset(obj_buf,0,sizeof(obj_buf));
    lc_data=lc_code=0;
    pass2();
    printf("         Done. Data: %d bytes  |  Code: %d bytes\n",lc_data,lc_code);

    print_symbol_table();
    print_proc_table();
    print_error_table();
    print_listing();

    printf("\n");
    if(err_count==0){
        write_obj("output8086_pr5.obj");
        printf("  Object file written: output8086_pr5.obj\n");
    } else {
        printf("  %d error(s) – object file NOT written.\n",err_count);
    }
    sep('=',72);
    return err_count?1:0;
}
