/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2015-2016 Josh Poimboeuf <jpoimboe@redhat.com>
 */

#ifndef _KLP_POST_ELF_H
#define _KLP_POST_ELF_H

#include <stdio.h>
#include <stdbool.h>
#include <gelf.h>
#include "list.h"

#ifdef LIBELF_USE_DEPRECATED
# define elf_getshdrnum    elf_getshnum
# define elf_getshdrstrndx elf_getshstrndx
#endif

struct section {
	struct list_head list;
	GElf_Shdr sh;
	struct section *base, *rela;
	struct list_head relas;
	struct symbol *sym;
	Elf_Data *elf_data;
	char *name;
	int idx;
	void *data;
	unsigned int size;
};

struct symbol {
	struct list_head list;
	GElf_Sym sym;
	struct section *sec;
	char *name;
	unsigned int idx;
	unsigned char bind, type;
	unsigned long offset;
	unsigned int size;
};

struct rela {
	struct list_head list;
	GElf_Rela rela;
	struct symbol *sym;
	unsigned int type;
	unsigned long offset;
	int addend;
};

struct elf {
	Elf *elf;
	GElf_Ehdr ehdr;
	int fd;
	char *name;
	int elf_class;
	struct list_head sections;
	struct list_head symbols;
};


struct elf *elf_open(const char *name);
bool is_rela_section(struct section *sec);
struct section *find_section_by_name(struct elf *elf, const char *name);
struct section *create_rela_section(struct elf *elf, const char *name,
				    struct section *base);

void elf_close(struct elf *elf);
int elf_write_file(struct elf *elf, const char *file);


#endif /* _KLP_POST_ELF_H */
