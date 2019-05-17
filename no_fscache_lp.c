// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
// Copyright (c) 2019, Jianshen Liu

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/livepatch.h>

#include <linux/seq_file.h>
static int livepatch_cmdline_proc_show(struct seq_file *m, void *v)
{
	seq_printf(m, "%s\n", "this has been live patched");
	return 0;
}

static struct klp_func funcs[] = { {
					   .old_name = "cmdline_proc_show",
					   .new_func =
						   livepatch_cmdline_proc_show,
				   },
				   {} };

static struct klp_object objs[] = { {
					    /* name being NULL means vmlinux */
					    .funcs = funcs,
				    },
				    {} };

static struct klp_patch patch = {
	.mod = THIS_MODULE,
	.objs = objs,
};

static int no_fscache_init(void)
{
	return klp_enable_patch(&patch);
}

static void no_fscache_exit(void)
{
}

module_init(no_fscache_init);
module_exit(no_fscache_exit);

MODULE_DESCRIPTION("Example module hooking clone() and execve() via ftrace");
MODULE_VERSION("0.1");
MODULE_AUTHOR("Jianshen Liu <jliu120@ucsc.edu>");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_INFO(livepatch, "Y");
