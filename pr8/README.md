Below is a **README** for Assignment 7, which covers the ELF analysis, instrumentation, and profiling tasks. It explains the assignment background, build/run steps, and a list of possible TA questions.

---

# Assignment 7: ELF File Analysis, Instrumentation, and Profiling

## 1. What this assignment was based on

This assignment focuses on the **Executable and Linkable Format (ELF)** – the standard binary format on Linux/Unix systems. The goal is to:

- **Parse and display** ELF headers, program headers, section headers, and symbol tables (like `readelf` or `objdump`).
- **Instrument** an existing ELF binary by adding a custom section (`.prof_list`) that lists function entry points for profiling.
- **Implement runtime profiling** using a `LD_PRELOAD` shared library that intercepts function calls, measures execution time, and records call counts.

The assignment builds on concepts from systems programming:
- Binary file parsing (memory‑mapped I/O, ELF structures)
- Dynamic linking and symbol interposition (`dlsym`, `LD_PRELOAD`)
- Linker tricks (`--wrap` or `objcopy` to add sections)
- Performance measurement (clock_gettime)

## 2. Files provided

| File | Purpose |
|------|---------|
| `elfinfo.c` | Reads an ELF file and prints its headers, sections, and symbol table. |
| `elfinstrument.c` | Adds an empty `.prof_list` section to an ELF (using `objcopy`). Optionally can be extended to populate the section with function addresses. |
| `profiler.c` | A `LD_PRELOAD` library that wraps specific functions (e.g., `add`, `multiply`) and collects call counts and execution times. |
| `test.c` | Example program (add, multiply) to demonstrate profiling. |
| `Makefile` | Builds all tools and the profiler. |

## 3. Build and run

```bash
make                # builds elfinfo, elfinstrument, profiler.so
./elfinfo /bin/ls   # example: analyse ls binary
```

### Demonstrate profiling (using `--wrap` method)

```bash
# Compile test program
gcc -o test test.c

# Build instrumentation library
gcc -shared -fPIC -o libinstrument.so instrument.c -ldl   # see Step 4 below

# Re-link test with wrapping
gcc -Wl,--wrap=add,--wrap=multiply -o test_instr test.c -L. -linstrument -Wl,-rpath=.

# Run instrumented program
./test_instr
```

### Using `LD_PRELOAD` with the original test

```bash
LD_PRELOAD=./profiler.so ./test
```

## 4. Example output

```
add(5,3) = 8
multiply(5,3) = 15

=== Profile ===
add: 1 calls, 1234 ns total, avg 1234.00 ns
multiply: 1 calls, 987 ns total, avg 987.00 ns
```

## 5. Possible TA questions (and answers)

### Q1: What is the difference between program headers and section headers?

**A:** Program headers describe **segments** used at load time (runtime memory layout). Section headers describe **sections** used at link time (symbols, debug info, etc.). An executable may have both; a stripped binary can omit section headers but must have program headers.

### Q2: How does `LD_PRELOAD` work?

**A:** The dynamic linker loads libraries specified in `LD_PRELOAD` **before** any other libraries (including libc). When a symbol (function) is resolved, the preloaded library’s definition takes precedence, allowing us to wrap/intercept functions.

### Q3: Why did we use `objcopy` in `elfinstrument.c`?

**A:** `objcopy` is a simple way to add or modify sections without rewriting the whole ELF parser/writer. It preserves the original binary’s structure and is widely available (part of binutils). For a “from‑scratch” solution we would parse the ELF, insert a new section, and adjust header offsets.

### Q4: How can we profile a function that we don’t know at compile time?

**A:** We can parse the `.prof_list` section at runtime (using `dl_iterate_phdr` or by reading the binary directly). Then we generate a trampoline (using `mmap` with `PROT_EXEC` and a small assembly stub) that jumps to the original function after recording timestamps.

### Q5: What is the difference between `gcc -Wl,--wrap` and `LD_PRELOAD`?

**A:** `--wrap` is a link‑time trick: it redirects calls to `__wrap_symbol` and provides `__real_symbol`. It works **statically** (at link time) and does not require a preloaded library. `LD_PRELOAD` works **dynamically** (at load time) and can instrument already‑compiled binaries without relinking.

### Q6: Why does `elfinfo` print the same symbol multiple times in `.dynsym` and `.symtab`?

**A:** `.dynsym` (dynamic symbol table) contains symbols needed for dynamic linking (exported/imported). `.symtab` (full symbol table) contains all symbols (including static functions) and is usually stripped from production binaries.

### Q7: How do we measure accurate time inside a profiler?

**A:** Use `clock_gettime(CLOCK_MONOTONIC)` – it is not affected by system time changes and has nanosecond resolution. Subtract start from end to get elapsed nanoseconds.

### Q8: Can we profile recursive functions with this approach?

**A:** Yes, but the simple wrapper will count **each** call (including nested). The time measurement will include the time of the nested calls (which is correct for total time). For exclusive time you would need to track call depth.

### Q9: What happens if we try to instrument a stripped binary (no symbol table)?

**A:** `objcopy` can still add a section, but we would not know function names or addresses without parsing DWARF debug info. We could still instrument by address (e.g., using a hardcoded list from a map file).

### Q10: How would you avoid calling the profiler recursively inside the wrapper itself?

**A:** The wrapper uses a static pointer to the real function, obtained via `dlsym(RTLD_NEXT, ...)`. This pointer points to the **original** function, not the wrapper, so no recursion occurs.

---

## 6. Further improvements (for extra credit)

- Automatically extract all function names from the symbol table and generate wrappers.
- Read `.prof_list` from the ELF and hook functions dynamically.
- Support for ARM64 / other architectures.
- Output profiling data in JSON or CSV format.
- Use `perf_event_open` for hardware counter profiling.

---

This README should help you understand the assignment and prepare for the viva/TA session. Good luck!
