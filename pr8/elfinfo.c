/*
 * elfinfo.c – Print ELF header, program headers, section headers, symbol table.
 * Compile: gcc -o elfinfo elfinfo.c
 * Usage:   ./elfinfo <executable>
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <elf.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

static void print_ehdr(Elf64_Ehdr *eh) {
    printf("ELF Header:\n");
    printf("  Magic:   %02x %02x %02x %02x\n", eh->e_ident[0], eh->e_ident[1], eh->e_ident[2], eh->e_ident[3]);
    printf("  Class:   %s\n", eh->e_ident[EI_CLASS] == ELFCLASS64 ? "64-bit" : "32-bit");
    printf("  Data:    %s\n", eh->e_ident[EI_DATA] == ELFDATA2LSB ? "2's complement, little endian" : "big endian");
    printf("  Type:    0x%x\n", eh->e_type);
    printf("  Machine: 0x%x\n", eh->e_machine);
    printf("  Entry:   0x%lx\n", eh->e_entry);
    printf("  PhOff:   0x%lx\n", eh->e_phoff);
    printf("  ShOff:   0x%lx\n", eh->e_shoff);
    printf("  PhNum:   %d\n", eh->e_phnum);
    printf("  ShNum:   %d\n", eh->e_shnum);
    printf("  ShStrNdx:%d\n\n", eh->e_shstrndx);
}

static void print_phdr(Elf64_Phdr *ph, int num, char *base) {
    printf("Program Headers:\n");
    for (int i = 0; i < num; i++) {
        printf("  %2d: type 0x%x, off 0x%lx, vaddr 0x%lx, paddr 0x%lx, filesz %ld, memsz %ld, flags %x, align %ld\n",
               i, ph[i].p_type, ph[i].p_offset, ph[i].p_vaddr, ph[i].p_paddr,
               ph[i].p_filesz, ph[i].p_memsz, ph[i].p_flags, ph[i].p_align);
    }
    printf("\n");
}

static void print_shdr(Elf64_Shdr *sh, int num, char *base, char *shstr) {
    printf("Section Headers:\n");
    for (int i = 0; i < num; i++) {
        const char *name = shstr + sh[i].sh_name;
        printf("  [%2d] %-15s addr 0x%lx, off 0x%lx, size %ld, link %d, info %d, align %ld\n",
               i, name, sh[i].sh_addr, sh[i].sh_offset, sh[i].sh_size,
               sh[i].sh_link, sh[i].sh_info, sh[i].sh_addralign);
    }
    printf("\n");
}

static void print_symtab(Elf64_Shdr *sh, int num, char *base, char *shstr) {
    for (int i = 0; i < num; i++) {
        if (sh[i].sh_type == SHT_SYMTAB || sh[i].sh_type == SHT_DYNSYM) {
            const char *name = shstr + sh[i].sh_name;
            printf("Symbol table '%s' (%d entries):\n", name,
                   (int)(sh[i].sh_size / sh[i].sh_entsize));
            Elf64_Sym *sym = (Elf64_Sym*)(base + sh[i].sh_offset);
            int count = sh[i].sh_size / sh[i].sh_entsize;
            // Find associated string table
            int str_idx = sh[i].sh_link;
            char *strtab = (char*)(base + sh[str_idx].sh_offset);
            for (int j = 0; j < count; j++) {
                if (sym[j].st_name != 0) {
                    const char *symname = strtab + sym[j].st_name;
                    printf("    %s: value 0x%lx, size %ld, type %d, bind %d\n",
                           symname, sym[j].st_value, sym[j].st_size,
                           ELF64_ST_TYPE(sym[j].st_info),
                           ELF64_ST_BIND(sym[j].st_info));
                }
            }
            printf("\n");
        }
    }
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <elf-file>\n", argv[0]);
        return 1;
    }
    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) { perror("open"); return 1; }
    struct stat st;
    fstat(fd, &st);
    char *base = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (base == MAP_FAILED) { perror("mmap"); close(fd); return 1; }
    Elf64_Ehdr *eh = (Elf64_Ehdr*)base;
    print_ehdr(eh);
    if (eh->e_phoff) {
        Elf64_Phdr *ph = (Elf64_Phdr*)(base + eh->e_phoff);
        print_phdr(ph, eh->e_phnum, base);
    }
    if (eh->e_shoff) {
        Elf64_Shdr *sh = (Elf64_Shdr*)(base + eh->e_shoff);
        char *shstr = base + sh[eh->e_shstrndx].sh_offset;
        print_shdr(sh, eh->e_shnum, base, shstr);
        print_symtab(sh, eh->e_shnum, base, shstr);
    }
    munmap(base, st.st_size);
    close(fd);
    return 0;
}
