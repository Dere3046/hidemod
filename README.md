# hidemod

LKM self-hiding library for ARM64 GKI kernels. removes a loaded
module from userspace and kernel-level inspection: module_list
unlink kills /proc/modules, kallsyms module symbols and
/sys/module in one shot, sysfs dir and BTF file are deleted,
mod_tree by addr lookup is removed, struct module fields can be
zeroed. symbol resolution is injected by the consumer (for example
KallRecon), no resolver bundled.

## license

GPL-2.0
