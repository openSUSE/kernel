// SPDX-License-Identifier: GPL-2.0
/*
 * elf.c - ELF access library
 *
 * Adapted from kpatch (https://github.com/dynup/kpatch):
 * Copyright (C) 2013-2016 Josh Poimboeuf <jpoimboe@redhat.com>
 * Copyright (C) 2014 Seth Jennings <sjenning@redhat.com>
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "elf.h"

#define WARN(format, ...) \
	fprintf(stderr, "%s: " format "\n", elf->name, ##__VA_ARGS__)

/*
 * Fallback for systems without this "read, mmaping if possible" cmd.
 */
#ifndef ELF_C_READ_MMAP
#define ELF_C_READ_MMAP ELF_C_READ
#endif

bool is_rela_section(struct section *sec)
{
	return (sec->sh.sh_type == SHT_RELA);
}

struct section *find_section_by_name(struct elf *elf, const char *name)
{
	struct section *sec;

	list_for_each_entry(sec, &elf->sections, list)
		if (!strcmp(sec->name, name))
			return sec;

	return NULL;
}

static struct section *find_section_by_index(struct elf *elf,
					     int idx)
{
	struct section *sec;

	list_for_each_entry(sec, &elf->sections, list)
		if (sec->idx == idx)
			return sec;

	return NULL;
}

static struct symbol *find_symbol_by_index(struct elf *elf, unsigned int idx)
{
	struct symbol *sym;

	list_for_each_entry(sym, &elf->symbols, list)
		if (sym->idx == idx)
			return sym;

	return NULL;
}

static int read_sections(struct elf *elf)
{
	Elf_Scn *s = NULL;
	struct section *sec;
	size_t shstrndx, sections_nr;
	size_t i;

	if (elf_getshdrnum(elf->elf, &sections_nr)) {
		perror("elf_getshdrnum");
		return -1;
	}

	if (elf_getshdrstrndx(elf->elf, &shstrndx)) {
		perror("elf_getshdrstrndx");
		return -1;
	}

	for (i = 0; i < sections_nr; i++) {
		sec = calloc(1, sizeof(*sec));
		if (!sec) {
			perror("calloc");
			return -1;
		}

		INIT_LIST_HEAD(&sec->relas);

		list_add_tail(&sec->list, &elf->sections);

		s = elf_getscn(elf->elf, i);
		if (!s) {
			perror("elf_getscn");
			return -1;
		}

		sec->idx = elf_ndxscn(s);

		if (!gelf_getshdr(s, &sec->sh)) {
			perror("gelf_getshdr");
			return -1;
		}

		sec->name = elf_strptr(elf->elf, shstrndx, sec->sh.sh_name);
		if (!sec->name) {
			perror("elf_strptr");
			return -1;
		}

		sec->elf_data = elf_getdata(s, NULL);
		if (!sec->elf_data) {
			perror("elf_getdata");
			return -1;
		}

		if (sec->elf_data->d_off != 0 ||
		    sec->elf_data->d_size != sec->sh.sh_size) {
			WARN("unexpected data attributes for %s", sec->name);
			return -1;
		}

		sec->data = sec->elf_data->d_buf;
		sec->size = sec->elf_data->d_size;
	}

	/* sanity check, one more call to elf_nextscn() should return NULL */
	if (elf_nextscn(elf->elf, s)) {
		WARN("section entry mismatch");
		return -1;
	}

	return 0;
}

static int read_symbols(struct elf *elf)
{
	struct section *symtab;
	struct symbol *sym;
	int symbols_nr, i;

	symtab = find_section_by_name(elf, ".symtab");
	if (!symtab) {
		WARN("missing symbol table");
		return -1;
	}

	symbols_nr = symtab->sh.sh_size / symtab->sh.sh_entsize;

	for (i = 0; i < symbols_nr; i++) {
		sym = calloc(1, sizeof(*sym));
		if (!sym) {
			perror("calloc");
			return -1;
		}

		sym->idx = i;

		if (!gelf_getsym(symtab->elf_data, i, &sym->sym)) {
			perror("gelf_getsym");
			goto err;
		}

		sym->name = elf_strptr(elf->elf, symtab->sh.sh_link,
				       sym->sym.st_name);
		if (!sym->name) {
			perror("elf_strptr");
			goto err;
		}

		sym->type = GELF_ST_TYPE(sym->sym.st_info);
		sym->bind = GELF_ST_BIND(sym->sym.st_info);

		if (sym->sym.st_shndx > SHN_UNDEF &&
		    sym->sym.st_shndx < SHN_LORESERVE) {
			sym->sec = find_section_by_index(elf,
							 sym->sym.st_shndx);
			if (!sym->sec) {
				WARN("couldn't find section for symbol %s",
				     sym->name);
				goto err;
			}
			if (sym->type == STT_SECTION) {
				sym->name = sym->sec->name;
				sym->sec->sym = sym;
			}
		}

		sym->offset = sym->sym.st_value;
		sym->size = sym->sym.st_size;

		list_add_tail(&sym->list, &elf->symbols);
	}

	return 0;

err:
	free(sym);
	return -1;
}

static int read_relas(struct elf *elf)
{
	struct section *sec;
	struct rela *rela;
	int relas_nr, i;
	unsigned int symndx;

	list_for_each_entry(sec, &elf->sections, list) {
		if (sec->sh.sh_type != SHT_RELA)
			continue;

		sec->base = find_section_by_name(elf, sec->name + 5);
		if (!sec->base) {
			WARN("can't find base section for rela section %s",
			     sec->name);
			return -1;
		}

		sec->base->rela = sec;

		relas_nr = sec->sh.sh_size / sec->sh.sh_entsize;
		for (i = 0; i < relas_nr; i++) {
			rela = calloc(1, sizeof(*rela));
			if (!rela) {
				perror("calloc");
				return -1;
			}

			if (!gelf_getrela(sec->elf_data, i, &rela->rela)) {
				perror("gelf_getrela");
				return -1;
			}

			rela->type = GELF_R_TYPE(rela->rela.r_info);
			rela->addend = rela->rela.r_addend;
			rela->offset = rela->rela.r_offset;
			symndx = GELF_R_SYM(rela->rela.r_info);
			rela->sym = find_symbol_by_index(elf, symndx);
			if (!rela->sym) {
				WARN("can't find rela entry symbol %u for %s",
				     symndx, sec->name);
				return -1;
			}

			list_add_tail(&rela->list, &sec->relas);
		}
	}

	return 0;
}

struct section *create_rela_section(struct elf *elf, const char *name,
				    struct section *base)
{
	struct section *sec;

	sec = calloc(1, sizeof(*sec));
	if (!sec) {
		WARN("calloc failed");
		return NULL;
	}
	INIT_LIST_HEAD(&sec->relas);

	sec->base = base;
	sec->name = strdup(name);
	if (!sec->name) {
		WARN("strdup failed");
		return NULL;
	}
	sec->sh.sh_name = ~0;
	sec->sh.sh_type = SHT_RELA;

	if (elf->elf_class == ELFCLASS32) {
		sec->sh.sh_entsize = sizeof(Elf32_Rela);
		sec->sh.sh_addralign = 4;
	} else {
		sec->sh.sh_entsize = sizeof(Elf64_Rela);
		sec->sh.sh_addralign = 8;
	}
	sec->sh.sh_flags = SHF_ALLOC;

	sec->elf_data = calloc(1, sizeof(*sec->elf_data));
	if (!sec->elf_data) {
		WARN("calloc failed");
		return NULL;
	}
	sec->elf_data->d_type = ELF_T_RELA;

	list_add_tail(&sec->list, &elf->sections);

	return sec;
}

static int update_shstrtab(struct elf *elf)
{
	struct section *shstrtab, *sec;
	size_t orig_size, new_size = 0, offset, len;
	char *buf;

	shstrtab = find_section_by_name(elf, ".shstrtab");
	if (!shstrtab) {
		WARN("can't find .shstrtab");
		return -1;
	}

	orig_size = new_size = shstrtab->size;

	list_for_each_entry(sec, &elf->sections, list) {
		if (sec->sh.sh_name != ~0U)
			continue;
		new_size += strlen(sec->name) + 1;
	}

	if (new_size == orig_size)
		return 0;

	buf = malloc(new_size);
	if (!buf) {
		WARN("malloc failed");
		return -1;
	}
	memcpy(buf, (void *)shstrtab->data, orig_size);

	offset = orig_size;
	list_for_each_entry(sec, &elf->sections, list) {
		if (sec->sh.sh_name != ~0U)
			continue;
		sec->sh.sh_name = offset;
		len = strlen(sec->name) + 1;
		memcpy(buf + offset, sec->name, len);
		offset += len;
	}

	shstrtab->elf_data->d_buf = shstrtab->data = buf;
	shstrtab->elf_data->d_size = shstrtab->size = new_size;
	shstrtab->sh.sh_size = new_size;

	return 1;
}

static void free_shstrtab(struct elf *elf)
{
	struct section *shstrtab;

	shstrtab = find_section_by_name(elf, ".shstrtab");
	if (!shstrtab)
		return;

	free(shstrtab->elf_data->d_buf);
}

static int update_strtab(struct elf *elf)
{
	struct section *strtab;
	struct symbol *sym;
	size_t orig_size, new_size = 0, offset, len;
	char *buf;

	strtab = find_section_by_name(elf, ".strtab");
	if (!strtab) {
		WARN("can't find .strtab");
		return -1;
	}

	orig_size = new_size = strtab->size;

	list_for_each_entry(sym, &elf->symbols, list) {
		if (sym->sym.st_name != ~0U)
			continue;
		new_size += strlen(sym->name) + 1;
	}

	if (new_size == orig_size)
		return 0;

	buf = malloc(new_size);
	if (!buf) {
		WARN("malloc failed");
		return -1;
	}
	memcpy(buf, (void *)strtab->data, orig_size);

	offset = orig_size;
	list_for_each_entry(sym, &elf->symbols, list) {
		if (sym->sym.st_name != ~0U)
			continue;
		sym->sym.st_name = offset;
		len = strlen(sym->name) + 1;
		memcpy(buf + offset, sym->name, len);
		offset += len;
	}

	strtab->elf_data->d_buf = strtab->data = buf;
	strtab->elf_data->d_size = strtab->size = new_size;
	strtab->sh.sh_size = new_size;

	return 1;
}

static void free_strtab(struct elf *elf)
{
	struct section *strtab;

	strtab = find_section_by_name(elf, ".strtab");
	if (!strtab)
		return;

	if (strtab->elf_data)
		free(strtab->elf_data->d_buf);
}

static int update_symtab(struct elf *elf)
{
	struct section *symtab, *sec;
	struct symbol *sym;
	char *buf;
	size_t size;
	int offset = 0, nr_locals = 0, idx, nr_syms;

	idx = 0;
	list_for_each_entry(sec, &elf->sections, list)
		sec->idx = idx++;

	idx = 0;
	list_for_each_entry(sym, &elf->symbols, list) {
		sym->idx = idx++;
		if (sym->sec)
			sym->sym.st_shndx = sym->sec->idx;
	}
	nr_syms = idx;

	symtab = find_section_by_name(elf, ".symtab");
	if (!symtab) {
		WARN("can't find symtab");
		return -1;
	}

	symtab->sh.sh_link = find_section_by_name(elf, ".strtab")->idx;

	/* create new symtab buffer */
	if (elf->elf_class == ELFCLASS32)
		size = nr_syms * sizeof(Elf32_Sym);
	else
		size = nr_syms * sizeof(Elf64_Sym);
	buf = calloc(1, size);
	if (!buf) {
		WARN("calloc failed");
		return -1;
	}

	offset = 0;
	list_for_each_entry(sym, &elf->symbols, list) {

		if (elf->elf_class == ELFCLASS32) {
			/* Manually convert to 32-bit Elf32_Sym */
			Elf32_Sym sym32;

			sym32.st_name  = sym->sym.st_name;
			sym32.st_info  = sym->sym.st_info;
			sym32.st_other = sym->sym.st_other;
			sym32.st_shndx = sym->sym.st_shndx;
			sym32.st_value = sym->sym.st_value;
			sym32.st_size  = sym->sym.st_size;
			memcpy(buf + offset, &sym32, sizeof(Elf32_Sym));
		} else {
			/* Existing 64-bit GElf_Syms are fine */
			memcpy(buf + offset, &sym->sym, sizeof(Elf64_Sym));
		}

		offset += symtab->sh.sh_entsize;

		if (sym->bind == STB_LOCAL)
			nr_locals++;
	}

	symtab->elf_data->d_buf = symtab->data = buf;
	symtab->elf_data->d_size = symtab->size = size;
	symtab->sh.sh_size = size;

	/* update symtab section header */
	symtab->sh.sh_info = nr_locals;

	return 1;
}

static void free_symtab(struct elf *elf)
{
	struct section *symtab;

	symtab = find_section_by_name(elf, ".symtab");
	if (!symtab)
		return;

	free(symtab->elf_data->d_buf);
}

static int update_relas(struct elf *elf)
{
	struct section *sec, *symtab;
	struct rela *rela;
	int nr_relas, idx, size;
	void *relas;

	symtab = find_section_by_name(elf, ".symtab");

	list_for_each_entry(sec, &elf->sections, list) {
		if (!is_rela_section(sec))
			continue;

		sec->sh.sh_link = symtab->idx;
		if (sec->base)
			sec->sh.sh_info = sec->base->idx;

		nr_relas = 0;
		list_for_each_entry(rela, &sec->relas, list)
			nr_relas++;

		if (elf->elf_class == ELFCLASS32)
			size = nr_relas * sizeof(Elf32_Rela);
		else
			size = nr_relas * sizeof(Elf64_Rela);

		relas = malloc(size);
		if (!relas) {
			WARN("malloc failed");
			return -1;
		}

		sec->elf_data->d_buf = sec->data = relas;
		sec->elf_data->d_size = sec->size = size;
		sec->sh.sh_size = size;

		idx = 0;
		list_for_each_entry(rela, &sec->relas, list) {
			if (elf->elf_class == ELFCLASS32) {
				Elf32_Rela *relas32 = relas;

				relas32[idx].r_offset = rela->offset;
				relas32[idx].r_addend = rela->addend;
				relas32[idx].r_info = ELF32_R_INFO(rela->sym->idx,
								   rela->type);
			} else {
				Elf64_Rela *relas64 = relas;

				relas64[idx].r_offset = rela->offset;
				relas64[idx].r_addend = rela->addend;
				relas64[idx].r_info = ELF64_R_INFO(rela->sym->idx,
								   rela->type);
			}
			idx++;
		}
	}

	return 1;
}

static void update_groups(struct elf *elf)
{
	struct section *sec, *symtab;

	symtab = find_section_by_name(elf, ".symtab");

	list_for_each_entry(sec, &elf->sections, list) {
		if (sec->sh.sh_type == SHT_GROUP)
			sec->sh.sh_link = symtab->idx;
	}
}

static void free_relas(struct elf *elf)
{
	struct section *sec, *symtab;

	symtab = find_section_by_name(elf, ".symtab");
	if (!symtab)
		return;

	list_for_each_entry(sec, &elf->sections, list) {
		if (!is_rela_section(sec))
			continue;

		free(sec->elf_data->d_buf);
	}
}

static int write_file(struct elf *elf, const char *file)
{
	int fd;
	Elf *e;
	GElf_Ehdr eh, ehout;
	Elf_Scn *scn;
	Elf_Data *data;
	GElf_Shdr sh;
	struct section *sec;

	fd = creat(file, 0664);
	if (fd == -1) {
		WARN("couldn't create %s", file);
		return -1;
	}

	e = elf_begin(fd, ELF_C_WRITE, NULL);
	if (!e) {
		WARN("elf_begin failed");
		return -1;
	}

	if (!gelf_newehdr(e, gelf_getclass(elf->elf))) {
		WARN("gelf_newehdr failed");
		return -1;
	}

	if (!gelf_getehdr(e, &ehout)) {
		WARN("gelf_getehdr failed");
		return -1;
	}

	if (!gelf_getehdr(elf->elf, &eh)) {
		WARN("gelf_getehdr failed");
		return -1;
	}

	memset(&ehout, 0, sizeof(ehout));
	ehout.e_ident[EI_DATA] = eh.e_ident[EI_DATA];
	ehout.e_machine = eh.e_machine;
	ehout.e_flags = eh.e_flags;
	ehout.e_type = eh.e_type;
	ehout.e_version = EV_CURRENT;
	ehout.e_shstrndx = find_section_by_name(elf, ".shstrtab")->idx;

	list_for_each_entry(sec, &elf->sections, list) {
		if (!sec->idx)
			continue;
		scn = elf_newscn(e);
		if (!scn) {
			WARN("elf_newscn failed");
			return -1;
		}

		data = elf_newdata(scn);
		if (!data) {
			WARN("elf_newdata failed");
			return -1;
		}

		if (!elf_flagdata(data, ELF_C_SET, ELF_F_DIRTY)) {
			WARN("elf_flagdata failed");
			return -1;
		}

		data->d_type = sec->elf_data->d_type;
		data->d_buf = sec->elf_data->d_buf;
		data->d_size = sec->elf_data->d_size;

		if (!gelf_getshdr(scn, &sh)) {
			WARN("gelf_getshdr failed");
			return -1;
		}

		sh = sec->sh;

		if (!gelf_update_shdr(scn, &sh)) {
			WARN("gelf_update_shdr failed");
			return -1;
		}
	}

	if (!gelf_update_ehdr(e, &ehout)) {
		WARN("gelf_update_ehdr failed");
		return -1;
	}

	if (elf_update(e, ELF_C_WRITE) < 0) {
		fprintf(stderr, "%s\n", elf_errmsg(-1));
		WARN("elf_update failed");
		return -1;
	}

	elf_end(e);

	return 0;
}

int elf_write_file(struct elf *elf, const char *file)
{
	int ret_shstrtab = 0;
	int ret_strtab = 0;
	int ret_symtab = 0;
	int ret_relas = 0;
	int ret;

	ret_shstrtab = update_shstrtab(elf);
	if (ret_shstrtab < 0) {
		ret = ret_shstrtab;
		goto out;
	}

	ret_strtab = update_strtab(elf);
	if (ret_strtab < 0) {
		ret = ret_strtab;
		goto out;
	}

	ret_symtab = update_symtab(elf);
	if (ret_symtab < 0) {
		ret = ret_symtab;
		goto out;
	}

	ret_relas = update_relas(elf);
	if (ret_relas < 0) {
		ret = ret_relas;
		goto out;
	}

	update_groups(elf);

	ret = write_file(elf, file);
	if (ret)
		return ret;

out:
	if (ret_relas > 0)
		free_relas(elf);
	if (ret_symtab > 0)
		free_symtab(elf);
	if (ret_strtab > 0)
		free_strtab(elf);
	if (ret_shstrtab > 0)
		free_shstrtab(elf);

	return ret;
}

struct elf *elf_open(const char *name)
{
	struct elf *elf;

	elf_version(EV_CURRENT);

	elf = calloc(1, sizeof(*elf));
	if (!elf) {
		perror("calloc");
		return NULL;
	}

	INIT_LIST_HEAD(&elf->sections);
	INIT_LIST_HEAD(&elf->symbols);

	elf->fd = open(name, O_RDONLY);
	if (elf->fd == -1) {
		perror("open");
		goto err;
	}

	elf->elf = elf_begin(elf->fd, ELF_C_READ_MMAP, NULL);
	if (!elf->elf) {
		perror("elf_begin");
		goto err;
	}

	if (!gelf_getehdr(elf->elf, &elf->ehdr)) {
		perror("gelf_getehdr");
		goto err;
	}

	elf->elf_class = gelf_getclass(elf->elf);
	if ((elf->elf_class != ELFCLASS32) && (elf->elf_class != ELFCLASS64)) {
		WARN("invalid elf class");
		goto err;
	}

	if (read_sections(elf))
		goto err;

	if (read_symbols(elf))
		goto err;

	if (read_relas(elf))
		goto err;

	return elf;

err:
	elf_close(elf);
	return NULL;
}

void elf_close(struct elf *elf)
{
	struct section *sec, *tmpsec;
	struct symbol *sym, *tmpsym;
	struct rela *rela, *tmprela;

	list_for_each_entry_safe(sym, tmpsym, &elf->symbols, list) {
		list_del(&sym->list);
		free(sym);
	}
	list_for_each_entry_safe(sec, tmpsec, &elf->sections, list) {
		list_for_each_entry_safe(rela, tmprela, &sec->relas, list) {
			list_del(&rela->list);
			free(rela);
		}
		list_del(&sec->list);
		free(sec);
	}
	if (elf->fd > 0)
		close(elf->fd);
	if (elf->elf)
		elf_end(elf->elf);
	free(elf);
}
