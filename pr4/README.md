**PR4 is complete — 0 errors, all features working.** Here's a quick summary of what was built:

**`assembler8086.c`** — the full 2-pass 8086 assembler:

**Pass 1** scans all source lines, assigns addresses to every label, and calculates the size of each data variable and code instruction. By the end of pass 1 the symbol table is fully populated.

**Pass 2** makes a second full scan, now resolving every symbol reference to its correct address and emitting actual object bytes.

**Symbol Table** tracks each label with its segment (DATA/CODE/STACK), offset, byte size, and kind (LABEL, VAR, or EQU).

**Error Table** collects every error with its line number — duplicate labels, undefined symbols, unsupported instructions, bad operand combinations — and prints them all at the end instead of stopping on the first one.

**Segment support** — `.DATA`, `.CODE`, `.STACK` are tracked separately with their own location counters. The flat object file records `DATA xxxx bb bb...` and `CODE xxxx bb bb...` lines so the loader knows where each segment's bytes belong.

**To build and run:**

bash

```bash
gcc -Wall -Wextra -o assembler8086 assembler8086.c
./assembler8086 test8086.asm
```

What are the remaining assignments after PR4? Share the titles and I'll continue building them in the same style.