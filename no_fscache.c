// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
// Copyright (c) 2019, Jianshen Liu

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/livepatch.h>

static asmlinkage long (*sys_read)(unsigned int fd, char __user *buf,
				   size_t count);
static asmlinkage long (*sys_write)(unsigned int fd, const char __user *buf,
				    size_t count);

static asmlinkage long no_fscache_sys_read(unsigned int fd, char __user *buf,
					   size_t count)
{
	/* pr_info("sys_read() before\n"); */
	return sys_read(fd, buf, count);
}

static asmlinkage long
no_fscache_sys_write(unsigned int fd, const char __user *buf, size_t count)
{
	return sys_write(fd, buf, count);
}

struct no_fscache_func {
	const char *old_name;
	void *orig_func;
	void *new_func;
};

#define NO_FSCACHE_FUNC(_old_name, _orig_func, _new_func)                      \
	{                                                                      \
		.old_name = (_old_name), .orig_func = (_orig_func),            \
		.new_func = (_new_func),                                       \
	}

static struct no_fscache_func nf_funcs[] = {
	NO_FSCACHE_FUNC("sys_read", &sys_read, no_fscache_sys_read),
	NO_FSCACHE_FUNC("sys_write", &sys_write, no_fscache_sys_write),
};

static struct klp_func funcs[ARRAY_SIZE(nf_funcs) + 1];

static struct klp_object objs[] = {
	{
		.name = NULL, /* name being NULL means vmlinux */
		.funcs = funcs,
	},
	{}
};

static struct klp_patch patch = {
	.mod = THIS_MODULE,
	.objs = objs,
};

static void fill_klp_func(struct klp_func *func,
			  struct no_fscache_func *nf_func)
{
	func->old_name = nf_func->old_name;
	func->new_func = nf_func->new_func;
}

static int fill_no_fscache_func(struct no_fscache_func *nf_func)
{
	unsigned long address = kallsyms_lookup_name(nf_func->old_name);

	if (!address) {
		pr_err("unresolved symbol: %s\n", nf_func->old_name);
		return -ENOENT;
	}

	*((unsigned long *)nf_func->orig_func) = address + MCOUNT_INSN_SIZE;

	return 0;
}

static int resolve_func(void)
{
	size_t count = ARRAY_SIZE(nf_funcs);
	size_t i;

	for (i = 0; i < count; i++) {
		struct klp_func *func = &funcs[i];
		struct no_fscache_func *nf_func = &nf_funcs[i];
		int ret;

		fill_klp_func(func, nf_func);

		ret = fill_no_fscache_func(nf_func);
		if (ret)
			return ret;
	}
	return 0;
}

static int no_fscache_init(void)
{
	int ret;

	ret = resolve_func();
	if (ret)
		return ret;

	ret = klp_register_patch(&patch);
	if (ret)
		return ret;

	ret = klp_enable_patch(&patch);
	if (ret) {
		WARN_ON(klp_unregister_patch(&patch));
		return ret;
	}

	return 0;
}

static void no_fscache_exit(void)
{
	WARN_ON(klp_unregister_patch(&patch));
}

module_init(no_fscache_init);
module_exit(no_fscache_exit);

MODULE_DESCRIPTION("Bypass the operating system read and write caches");
MODULE_VERSION("0.1");
MODULE_AUTHOR("Jianshen Liu <jliu120@ucsc.edu>");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_INFO(livepatch, "Y");
