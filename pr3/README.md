## Hypothetical Machine Assembler — Design Summary

### Machine Architecture

| Property           | Value                       |
| ------------------ | --------------------------- |
| Word size          | 16 bits                     |
| Address bus        | 12 bits (4096 words)        |
| Instruction format | `[OPCODE(4)] [OPERAND(12)]` |

### Instruction Set (11 opcodes)

|Mnemonic|Opcode|Operation|
|---|---|---|
|`LOAD addr`|`0x5`|`AC ← MEM[addr]`|
|`STOR addr`|`0x6`|`MEM[addr] ← AC`|
|`ADD addr`|`0x1`|`AC ← AC + MEM[addr]`|
|`SUB addr`|`0x2`|`AC ← AC − MEM[addr]`|
|`MUL addr`|`0x3`|`AC ← AC × MEM[addr]`|
|`DIV addr`|`0x4`|`AC ← AC ÷ MEM[addr]`|
|`JUMP addr`|`0x7`|`PC ← addr`|
|`JNEG addr`|`0x8`|Branch if AC < 0|
|`JZERO addr`|`0x9`|Branch if AC = 0|
|`HALT`|`0xA`|Stop execution|
|`NOP`|`0xB`|No operation|

### Directives

- `ORG val` — set location counter
- `DC val` — define constant word
- `DS n` — reserve n words (zeroed)
- `END` — end of source

### Single-Pass Forward Reference Resolution

The key algorithm: when a label is **used before it's defined**, the assembler:

1. Emits a placeholder word (opcode + `0x000`)
2. Adds the address to a **backpatch list** tied to that label name
3. When the label is **later defined**, all pending entries for it are patched immediately — updating the low 12 bits of the stored word in-place

### Build & Run

bash

```bash
gcc -Wall -Wextra -o assembler assembler.c
./assembler test.asm
```