// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
// Copyright (c) 2019, Jianshen Liu

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/livepatch.h>
#include <linux/module.h>

static asmlinkage long (*orig_sys_read)(unsigned int fd, char __user *buf,
					size_t count);
static asmlinkage long (*orig_sys_write)(unsigned int fd,
					 const char __user *buf, size_t count);

static ssize_t (*ksys_read)(unsigned int fd, char __user *buf, size_t count);

static asmlinkage long no_fscache_sys_read(unsigned int fd, char __user *buf,
					   size_t count)
{
	/* The problem is it somehow causes recursive call here. */
	return orig_sys_read(fd, buf, count);
}

static asmlinkage long
no_fscache_sys_write(unsigned int fd, const char __user *buf, size_t count)
{
	return orig_sys_write(fd, buf, count);
}

struct func_symbol {
	const char *name;
	void *func;
};

#define FUNC_SYMBOL(_name, _func)                                              \
	{                                                                      \
		.name = (_name), .func = (_func),                              \
	}

struct no_fscache_func {
	struct func_symbol fsym;
	void *new_func;
};

#define NO_FSCACHE_FUNC(_old_name, _orig_func, _new_func)                      \
	{                                                                      \
		.fsym = FUNC_SYMBOL(_old_name, _orig_func),                    \
		.new_func = (_new_func),                                       \
	}

static struct no_fscache_func nf_funcs[] = {
	NO_FSCACHE_FUNC("sys_read", &orig_sys_read, no_fscache_sys_read),
	NO_FSCACHE_FUNC("sys_write", &orig_sys_write, no_fscache_sys_write),
};

static struct func_symbol dept_fsyms[] = {
	FUNC_SYMBOL("ksys_read", &ksys_read),
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
	func->old_name = nf_func->fsym.name;
	func->new_func = nf_func->new_func;
}

static int fill_func_symbol(struct func_symbol *fsym, bool bypass_mcount)
{
	unsigned long address = kallsyms_lookup_name(fsym->name);

	if (!address) {
		pr_err("unresolved symbol: %s\n", fsym->name);
		return -ENOENT;
	}

	*((unsigned long *)fsym->func) = address;
	if (bypass_mcount)
		*((unsigned long *)fsym->func) += MCOUNT_INSN_SIZE;

	return 0;
}

static int resolve_func(void)
{
	size_t count = ARRAY_SIZE(nf_funcs);
	size_t i;
	int ret;

	for (i = 0; i < count; i++) {
		struct klp_func *func = &funcs[i];
		struct no_fscache_func *nf_func = &nf_funcs[i];

		fill_klp_func(func, nf_func);

		ret = fill_func_symbol(&nf_func->fsym, true);
		if (ret)
			return ret;
	}

	count = ARRAY_SIZE(dept_fsyms);

	for (i = 0; i < count; i++) {
		struct func_symbol *fsym = &dept_fsyms[i];

		ret = fill_func_symbol(fsym, false);
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
