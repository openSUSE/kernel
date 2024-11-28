// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2016 Josh Poimboeuf <jpoimboe@redhat.com>
 * Copyright (C) 2017 Joao Moreira   <jmoreira@suse.de>
 * Copyright (C) 2023 Lukas Hruska   <lhruska@suse.cz>
 */

#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include "elf.h"
#include "list.h"
#include "klp-convert.h"

#define KSYM_NAME_LEN 512

#define safe_snprintf(var, size, format, args...)			\
	({								\
		int __ret;						\
									\
		__ret = snprintf(var, size, format, ##args);		\
		__ret < 0 || (size_t)__ret >= size;			\
	})

/*
 * Formats name of klp rela symbol based on another given section (@oldsec)
 * and object (@obj_name) name, then returns it
 */
static char *alloc_klp_rela_name(struct section *oldsec,
		char *target_objname, struct elf *klp_elf)
{
	char *klp_rela_name;
	unsigned int length;
	int err;

    /*
     * Format: .klp.rela.sec_objname.section_name
     * Note: ".section_name" comes from oldsec->base->name
     *    including the dot.
     */
	length = strlen(KLP_RELA_PREFIX) + strlen(target_objname)
		 + strlen(oldsec->base->name) + 1;

	klp_rela_name = calloc(1, length);
	if (!klp_rela_name) {
		WARN("Memory allocation failed (%s%s%s)\n", KLP_RELA_PREFIX,
				target_objname, oldsec->base->name);
		return NULL;
	}

	err = safe_snprintf(klp_rela_name, length, KLP_RELA_PREFIX "%s%s",
			  target_objname, oldsec->base->name);
	if (err) {
		WARN("Length error (%s)", klp_rela_name);
		free(klp_rela_name);
		return NULL;
	}

	return klp_rela_name;
}

static int calc_digits(int num)
{
	int count = 0;

	/* It takes a digit to represent zero */
	if (!num)
		return 1;

	while (num != 0) {
		num /= 10;
		count++;
	}

	return count;
}

/* Converts rela symbol names */
static bool convert_symbol(struct symbol *s)
{
	char lp_obj_name[MODULE_NAME_LEN];
	char sym_obj_name[MODULE_NAME_LEN];
	char sym_name[KSYM_NAME_LEN];
	char *klp_sym_name;
	unsigned long sym_pos;
	int poslen;
	unsigned int length;

	static_assert(MODULE_NAME_LEN <= 56, "Update limit in the below sscanf()");

	if (sscanf(s->name, KLP_SYM_RELA_PREFIX "%55[^.].%55[^.].%511[^,],%lu",
			lp_obj_name, sym_obj_name, sym_name, &sym_pos) != 4) {
		WARN("Invalid format of symbol (%s)\n", s->name);
		return false;
	}

	poslen = calc_digits(sym_pos);

	length = strlen(KLP_SYM_PREFIX) + strlen(sym_obj_name)
		 + strlen(sym_name) + sizeof(poslen) + 3;

	klp_sym_name = calloc(1, length);
	if (!klp_sym_name) {
		WARN("Memory allocation failed (%s%s.%s,%lu)\n", KLP_SYM_PREFIX,
				sym_obj_name, sym_name, sym_pos);
		return false;
	}

	if (safe_snprintf(klp_sym_name, length, KLP_SYM_PREFIX "%s.%s,%lu",
			  sym_obj_name, sym_name, sym_pos)) {

		WARN("Length error (%s%s.%s,%lu)", KLP_SYM_PREFIX,
				sym_obj_name, sym_name, sym_pos);
		free(klp_sym_name);
		return false;
	}

	s->name = klp_sym_name;
	s->sec = NULL;
	s->sym.st_name = -1;
	s->sym.st_shndx = SHN_LIVEPATCH;

	return true;
}

/* Checks if a symbols was already converted */
static bool is_converted_symbol(struct symbol *sym)
{
	return sym->sym.st_shndx == SHN_LIVEPATCH;
}

/*
 * Finds or creates a klp rela section based on another given section (@oldsec)
 * and rela's symbol name (@rela), then returns it
 */
static struct section *get_or_create_klp_rela_section(struct section *oldsec, struct rela *rela,
		struct elf *klp_elf)
{
	char *klp_rela_name;
	char lp_obj_name[MODULE_NAME_LEN];
	struct section *sec;

	if (sscanf(rela->sym->name, KLP_SYM_RELA_PREFIX "%55[^.]", lp_obj_name) != 1) {
		WARN("Invalid relocation symbol name.\n");
		return NULL;
	}

	klp_rela_name = alloc_klp_rela_name(oldsec, lp_obj_name, klp_elf);
	if (!klp_rela_name) {
		WARN("Can't create or access klp.rela section (%s%s)\n",
				lp_obj_name, oldsec->base->name);
		return NULL;
	}

	sec = find_section_by_name(klp_elf, klp_rela_name);
	if (!sec)
		sec = create_rela_section(klp_elf, klp_rela_name, oldsec->base);

	if (sec)
		sec->sh.sh_flags |= SHF_RELA_LIVEPATCH;

	free(klp_rela_name);
	return sec;
}

static void move_rela(struct rela *r, struct section *rela_sec)
{
	/* Move the rela into newly created klp rela section */
	list_del(&r->list);
	list_add_tail(&r->list, &rela_sec->relas);
}

static bool is_klp_sym_rela_symbol(struct symbol *sym)
{
	int len;

	/* skip index 0 which serves as the undefined symbol index */
	if (!sym->idx)
		return false;

	len = strlen(KLP_SYM_RELA_PREFIX);
	/*
	 * we want to resolve only symbols with format:
	 * .klp.sym.rela.<target-obj-name>.<foo-providing-obj-name>.foo,0
	 */
	return strncmp(sym->name, KLP_SYM_RELA_PREFIX, len) == 0;
}

/* Checks if a section is a klp rela section */
static bool is_klp_rela_section(struct section *sec)
{
	if (!is_rela_section(sec))
		return false;

	int len = strlen(KLP_RELA_PREFIX);

	return strncmp(sec->name, KLP_RELA_PREFIX, len) == 0;
}

/*
 * Frees the new names and rela sections as created by
 * get_or_create_klp_rela_section(), and convert_symbol()
 */
static void free_converted_resources(struct elf *klp_elf)
{
	struct symbol *sym;
	struct section *sec;

	list_for_each_entry(sym, &klp_elf->symbols, list) {
		if (is_converted_symbol(sym))
			free(sym->name);
	}

	list_for_each_entry(sec, &klp_elf->sections, list) {
		if (is_klp_rela_section(sec)) {
			free(sec->elf_data);
			free(sec->name);
		}
	}
}

int main(int argc, const char **argv)
{
	const char *klp_in_module, *klp_out_module;
	struct rela *rela, *tmprela;
	struct section *sec, *rela_sec;
	struct elf *klp_elf;
	struct symbol *sym;

	if (argc != 3) {
		WARN("Usage: %s <input.ko> <output.ko>", argv[0]);
		return -1;
	}

	klp_in_module = argv[1];
	klp_out_module = argv[2];

	klp_elf = elf_open(klp_in_module);
	if (!klp_elf) {
		WARN("Unable to read elf file %s\n", klp_in_module);
		return -1;
	}

	list_for_each_entry(sec, &klp_elf->sections, list) {
		/* skip newly created sections */
		if (is_klp_rela_section(sec))
			continue;

		list_for_each_entry_safe(rela, tmprela, &sec->relas, list) {
			if (!is_klp_sym_rela_symbol(rela->sym))
				continue;

			rela_sec = get_or_create_klp_rela_section(sec, rela, klp_elf);
			if (!rela_sec) {
				WARN("Unable to convert relocation: %s",
						rela->sym->name);
				return -1;
			}
			/* rela needs to be moved to newly created section */
			move_rela(rela, rela_sec);
		}
	}

	/* Rename symbols */
	list_for_each_entry(sym, &klp_elf->symbols, list) {
		if (!is_klp_sym_rela_symbol(sym))
			continue;
		if (!convert_symbol(sym)) {
			WARN("Unable to convert symbol name (%s)\n",
					sym->name);
			return -1;
		}
	}

	if (elf_write_file(klp_elf, klp_out_module))
		return -1;

	free_converted_resources(klp_elf);
	elf_close(klp_elf);

	return 0;
}
