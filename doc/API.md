# hidemod API

call hm_init once with a resolver that resolves kernel symbol names.
register hide targets one by one, then hm_hide. hide is deferred to a
workqueue because do_init_module touches mkobj (kobject_uevent) and
re-runs mod_tree_remove_init after module init returns.

## Dependencies

symbol resolution comes from the consumer injected
hm_resolver.name_to_addr (for example KallRecon's kallrecon_klp). the
library does not bundle a resolver.

symbols resolved through the resolver:

- module_mutex: module list and mod_tree protection
- modules: module list head, fallback re-insert anchor
- mod_tree_insert / mod_tree_remove: by addr lookup
- btf_kobj: module BTF sysfs parent

missing optional symbols goto low level (affected actions are skipped).
kernel exported calls used: kobject_del, sysfs_remove_bin_file,
synchronize_rcu.

## Actions

module hiding is a combination of steps controlled by a mask set with
hm_set_actions. default HM_ACT_USER: module_list unlink, sysfs dir
removal, BTF file deletion. add HM_ACT_MOD_TREE and HM_ACT_FIELDS
(HM_ACT_ALL) for kernel-level inspection.

## Lifecycle

**int hm_init(const struct hm_resolver *res)**

res must provide name_to_addr. 0 on success, -EINVAL when res is NULL or
name_to_addr is missing.

**void hm_exit(void)**

flushes pending hide work and resets the library state.

## Register

**int hm_add_module(struct module *mod, struct list_head *anchor)**

hide a whole module: module_list unlink, sysfs dir removal, BTF file
removal, mod_tree removal, optional field zeroing. anchor is the re-insert
head; NULL falls back to resolve("modules").

**int hm_add_list(struct list_head *node, struct list_head *anchor)**

unlink any list node. anchor required for re-insert.

**int hm_add_kobj(struct kobject *kobj)**

remove a kobject from sysfs.

**int hm_add_mod_tree(struct module *mod)**

remove a module from the mod_tree by addr lookup.

**int hm_add_zero(unsigned long addr, size_t size)**

zero an address range. irreversible.

**void hm_clear_objs(void)**

drop all registered targets. use before hm_hide, not after.

**void hm_set_actions(unsigned long mask)**

set the hide step mask for HM_OBJ_MODULE targets: HM_ACT_LIST unlinks
module_list, HM_ACT_SYSFS removes the sysfs dir, HM_ACT_BTF deletes the
BTF file, HM_ACT_MOD_TREE removes mod_tree entries, HM_ACT_FIELDS zeroes
name/state/taints. default HM_ACT_USER. HM_ACT_ALL enables everything.

all hm_add_* return 0 on success, -ENOSPC when the registry is full
(HM_OBJ_MAX).

## Hide

**int hm_hide(void)**

commits the hide. returns 0 immediately; the hide runs a few ms later
on the workqueue.

**int hm_unhide(void)**

flushes the hide work and restores module_list, mod_tree entries,
module name and the sysfs dir (kset re-attached, kobject_add). BTF file
is not restored. after unhide the module unloads normally.

**bool hm_hidden(void)**

nonzero once hide was committed, zero after unhide.

## Rules

- rmmod fails while hidden: unhide first, then unload.
- do not call hm_add_* while hidden: the hide work reads the registry.
- hm_clear_objs before hide, never after.
