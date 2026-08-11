/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2026 dere3046
 */

#ifndef HIDEMOD_H
#define HIDEMOD_H

#include <linux/list.h>
#include <linux/kobject.h>
#include <linux/module.h>

#define HM_OBJ_MAX 16

enum hm_obj_type {
	HM_OBJ_MODULE,
	HM_OBJ_LIST,
	HM_OBJ_KOBJ,
	HM_OBJ_MOD_TREE,
	HM_OBJ_ZERO,
};

struct hm_obj {
	enum hm_obj_type type;
	union {
		struct module *mod;
		struct list_head *list;
		struct kobject *kobj;
		unsigned long addr;
	} u;
	size_t size;
	struct list_head *anchor;
};

struct hm_resolver {
	unsigned long (*name_to_addr)(const char *name);
};

int hm_init(const struct hm_resolver *res);
void hm_exit(void);

int hm_add_module(struct module *mod, struct list_head *anchor);
int hm_add_list(struct list_head *node, struct list_head *anchor);
int hm_add_kobj(struct kobject *kobj);
int hm_add_mod_tree(struct module *mod);
int hm_add_zero(unsigned long addr, size_t size);
void hm_clear_objs(void);
void hm_set_clear_fields(bool en);

int hm_hide(void);
int hm_unhide(void);
bool hm_hidden(void);

#endif
