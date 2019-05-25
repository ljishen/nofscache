// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
// Copyright (c) 2019, Jianshen Liu

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/fadvise.h>
#include <linux/file.h>
#include <linux/kernel.h>
#include <linux/livepatch.h>
#include <linux/module.h>

static asmlinkage long (*orig_sys_fadvise64)(int fd, loff_t offset, size_t len,
					     int advice);
static asmlinkage long (*orig_sys_read)(unsigned int fd, char __user *buf,
					size_t count);
static asmlinkage long (*orig_sys_write)(unsigned int fd,
					 const char __user *buf, size_t count);

static unsigned long __cp_fdget_pos(unsigned int fd)
{
	unsigned long v = __fdget(fd);
	struct file *file = (struct file *)(v & ~3);

	if (file && (file->f_mode & FMODE_ATOMIC_POS)) {
		if (file_count(file) > 1) {
			v |= FDPUT_POS_UNLOCK;
			mutex_lock(&file->f_pos_lock);
		}
	}
	return v;
}

static inline struct fd cp_fdget_pos(int fd)
{
	return __to_fd(__cp_fdget_pos(fd));
}

/*
 * File is stream-like
 * See https://elixir.bootlin.com/linux/v5.1.4/source/include/linux/fs.h#L162
 */
#define FMODE_STREAM ((__force fmode_t)0x200000)

static inline loff_t file_pos_read(struct file *file)
{
	return (file->f_mode & FMODE_STREAM) ? 0 : file->f_pos;
}

static inline void file_pos_write(struct file *file, loff_t pos)
{
	if ((file->f_mode & FMODE_STREAM) == 0)
		file->f_pos = pos;
}

static void __cp_f_unlock_pos(struct file *f)
{
	mutex_unlock(&f->f_pos_lock);
}

static inline void cp_fdput_pos(struct fd f)
{
	if (f.flags & FDPUT_POS_UNLOCK)
		__cp_f_unlock_pos(f.file);
	fdput(f);
}

static inline loff_t largest_page_offset_less_then(loff_t num)
{
	return num - (num & (PAGE_SIZE - 1));
}

static inline size_t smallest_page_offset_greater_then(size_t num)
{
	size_t rem = (num + PAGE_SIZE) & (PAGE_SIZE - 1);
	return rem == 0 ? num : num + PAGE_SIZE - rem;
}

static asmlinkage long no_fscache_sys_read(unsigned int fd, char __user *buf,
					   size_t count)
{
	struct fd f = cp_fdget_pos(fd);
	ssize_t ret = -EBADF;

	if (f.file) {
		loff_t pos = file_pos_read(f.file);
		loff_t fpos = pos;

		ret = vfs_read(f.file, buf, count, &pos);
		if (ret >= 0)
			file_pos_write(f.file, pos);
		cp_fdput_pos(f);

		// If the number of bytes read is greater than 0
		if (ret > 0) {
			/*
			 *  offset and len must be cache page aligned.
			 *  Partial pages are deliberately be ignored.
			 *  https://elixir.bootlin.com/linux/v5.1.4/source/mm/fadvise.c#L120
			 *
			 *  Because the this, read with unaligned buffer size
			 *  will suffer from significant performance
			 *  degradation.
			 */
			loff_t offset = largest_page_offset_less_then(fpos);
			size_t len = smallest_page_offset_greater_then(ret);

			orig_sys_fadvise64(fd, offset, len,
					   POSIX_FADV_DONTNEED);
		}
	}
	return ret;
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
	FUNC_SYMBOL("sys_fadvise64", &orig_sys_fadvise64),
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
