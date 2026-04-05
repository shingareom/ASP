/*
 * ============================================================
 *  Bootstrap Loader for 8086 Object File (output8086.obj)
 * ============================================================
 *
 *  This loader reads the object file produced by the
 *  two-pass assembler (PR4) and loads the DATA and CODE
 *  segments into a flat 8086 memory model (real mode, 1 MB).
 *
 *  Features:
 *    - Parses header comments to obtain:
 *        * Stack size
 *        * Data segment base address
 *        * Code segment base address
 *    - Loads DATA segment bytes at DS:offset (base + offset)
 *    - Loads CODE segment bytes at CS:offset (base + offset)
 *    - Reserves stack area (initial SS:SP = stack_base + stack_size)
 *    - Default entry point: CS:IP = code_base (offset 0)
 *    - Displays loaded memory ranges and entry point
 *
 *  Usage:
 *    ./bootstrap_loader [object_file.obj]
 *    (default: output8086.obj)
 *
 *  Build:
 *    gcc -Wall -Wextra -o bootstrap_loader bootstrap_loader.c
 *
 *  Note: This loader does NOT execute the loaded code.
 *        It only loads it into a memory buffer and reports success.
 *        Execution can be added via a simple 8086 emulator.
 * ============================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MEMORY_SIZE     0x100000   /* 1 MB (8086 real mode) */
#define MAX_LINE        256
#define MAX_TOKENS      32

/* Memory buffer */
static unsigned char memory[MEMORY_SIZE];

/* Segment bases (computed from object file header) */
static int stack_size   = 256;    /* default 256 bytes */
static int data_base    = 0;      /* base address of DATA segment */
static int code_base    = 0;      /* base address of CODE segment */

/* Loaded segment sizes */
static int data_size    = 0;
static int code_size    = 0;

/* ------------------------------------------------------------------
 *  Helper: trim whitespace
 */
static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    if (*s == '\0') return s;
    char *end = s + strlen(s) - 1;
    while (end > s && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return s;
}

/* ------------------------------------------------------------------
 *  Parse header comments from object file to obtain:
 *    - Stack size
 *    - Data segment base
 *    - Code segment base
 */
static int parse_header(FILE *fp) {
    char line[MAX_LINE];
    int found_stack = 0, found_data = 0, found_code = 0;

    /* Rewind to beginning */
    rewind(fp);

    while (fgets(line, sizeof(line), fp)) {
        /* Look for lines starting with ';' */
        if (line[0] != ';') break;   /* header ends at first non‑comment */

        /* Stack size */
        if (strstr(line, "Stack size") != NULL) {
            int val;
            if (sscanf(line, "; Stack size : %d bytes", &val) == 1) {
                stack_size = val;
                found_stack = 1;
            }
        }
        /* Data base */
        else if (strstr(line, "Data  seg  : base") != NULL) {
            int val;
            if (sscanf(line, "; Data  seg  : base 0x%x", &val) == 1) {
                data_base = val;
                found_data = 1;
            }
        }
        /* Code base */
        else if (strstr(line, "Code  seg  : base") != NULL) {
            int val;
            if (sscanf(line, "; Code  seg  : base 0x%x", &val) == 1) {
                code_base = val;
                found_code = 1;
            }
        }
    }

    if (!found_stack) fprintf(stderr, "Warning: Stack size not found, using default %d\n", stack_size);
    if (!found_data)  fprintf(stderr, "Warning: Data base not found, using 0\n");
    if (!found_code)  fprintf(stderr, "Warning: Code base not found, using 0\n");

    return (found_data && found_code) ? 0 : -1;
}

/* ------------------------------------------------------------------
 *  Parse one line of the form:
 *      SEGMENT  OFFSET  HEXBYTES...
 *  where SEGMENT is "DATA" or "CODE", OFFSET is hex number,
 *  and HEXBYTES are space‑separated two‑digit hex values.
 */
static int parse_line(const char *line, char *seg, int *offset, unsigned char *bytes, int *byte_count) {
    char buf[MAX_LINE];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    /* Tokenise */
    char *tok = strtok(buf, " \t");
    if (!tok) return -1;
    strcpy(seg, tok);              /* "DATA" or "CODE" */

    tok = strtok(NULL, " \t");
    if (!tok) return -1;
    *offset = (int)strtol(tok, NULL, 16);   /* offset in hex */

    *byte_count = 0;
    while ((tok = strtok(NULL, " \t")) != NULL && *byte_count < 16) {
        unsigned int byte;
        if (sscanf(tok, "%02x", &byte) == 1) {
            bytes[(*byte_count)++] = (unsigned char)byte;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------
 *  Load bytes into memory at the appropriate base + offset.
 */
static void load_bytes(const char *seg, int offset, const unsigned char *bytes, int byte_count) {
    int base = 0;
    if (strcmp(seg, "DATA") == 0) {
        base = data_base;
        if (offset + byte_count > data_size) data_size = offset + byte_count;
    } else if (strcmp(seg, "CODE") == 0) {
        base = code_base;
        if (offset + byte_count > code_size) code_size = offset + byte_count;
    } else {
        fprintf(stderr, "Unknown segment: %s\n", seg);
        return;
    }

    int addr = base + offset;
    if (addr + byte_count > MEMORY_SIZE) {
        fprintf(stderr, "Error: Address 0x%X exceeds memory limit\n", addr);
        return;
    }

    for (int i = 0; i < byte_count; i++) {
        memory[addr + i] = bytes[i];
    }
}

/* ------------------------------------------------------------------
 *  Load the entire object file.
 */
static int load_object_file(const char *filename) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        perror(filename);
        return -1;
    }

    /* First, parse header to get segment bases */
    if (parse_header(fp) != 0) {
        fprintf(stderr, "Failed to parse header. Is this a valid object file?\n");
        fclose(fp);
        return -1;
    }

    /* Now read the actual data/code lines */
    char line[MAX_LINE];
    int line_num = 0;
    while (fgets(line, sizeof(line), fp)) {
        line_num++;
        char *trimmed = trim(line);
        if (trimmed[0] == '\0' || trimmed[0] == ';') continue; /* skip empty or comment */

        char seg[8];
        int offset;
        unsigned char bytes[16];
        int byte_count;

        if (parse_line(trimmed, seg, &offset, bytes, &byte_count) == 0) {
            load_bytes(seg, offset, bytes, byte_count);
        } else {
            fprintf(stderr, "Warning: cannot parse line %d: %s\n", line_num, trimmed);
        }
    }

    fclose(fp);
    return 0;
}

/* ------------------------------------------------------------------
 *  Display loaded memory regions.
 */
static void display_memory_map(void) {
    printf("\n============================================================\n");
    printf("  BOOTSTRAP LOADER – Memory Load Summary\n");
    printf("============================================================\n");
    printf("  Stack      : %d bytes (0x%04X – 0x%04X)\n",
           stack_size, 0, stack_size - 1);
    printf("  Data base  : 0x%04X, loaded %d bytes (0x%04X – 0x%04X)\n",
           data_base, data_size, data_base, data_base + data_size - 1);
    printf("  Code base  : 0x%04X, loaded %d bytes (0x%04X – 0x%04X)\n",
           code_base, code_size, code_base, code_base + code_size - 1);
    printf("  Entry point: CS:IP = 0x%04X:0x0000\n", code_base);
    printf("============================================================\n");
}

/* ------------------------------------------------------------------
 *  Optional: Dump a range of memory (for debugging)
 */
static void dump_memory(int start, int end) {
    printf("\nMemory dump 0x%04X – 0x%04X:\n", start, end);
    for (int addr = start; addr <= end; addr += 16) {
        printf("%04X: ", addr);
        for (int k = 0; k < 16 && addr + k <= end; k++) {
            printf("%02X ", memory[addr + k]);
        }
        printf("\n");
    }
}

/* ------------------------------------------------------------------
 *  Main
 */
int main(int argc, char *argv[]) {
    const char *obj_filename = "output8086.obj";
    if (argc >= 2) obj_filename = argv[1];

    printf("8086 Bootstrap Loader\n");
    printf("Object file: %s\n\n", obj_filename);

    /* Clear memory */
    memset(memory, 0, sizeof(memory));

    /* Load object file */
    if (load_object_file(obj_filename) != 0) {
        fprintf(stderr, "Loading failed.\n");
        return 1;
    }

    /* Display loaded memory map */
    display_memory_map();

    /* Optionally dump first few bytes of code segment */
    printf("\nFirst 16 bytes of loaded code:\n");
    dump_memory(code_base, code_base + (code_size > 16 ? 15 : code_size - 1));

    printf("\nBootstrap loading completed successfully.\n");
    printf("To execute the loaded program, transfer control to CS:IP = 0x%04X:0x0000\n", code_base);

    return 0;
}
