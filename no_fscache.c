// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
// Copyright (c) 2019, Jianshen Liu

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/fadvise.h>
#include <linux/file.h>
#include <linux/kernel.h>
#include <linux/livepatch.h>
#include <linux/module.h>

/*
 * Functions we need but are not exported to be used in loadable modules by
 * kernel.
 */
static asmlinkage long (*orig_sys_fadvise64_64)(int fd, loff_t offset,
						size_t len, int advice);
static asmlinkage long (*orig_sys_sync_file_range2)(int fd, unsigned int flags,
						    loff_t offset,
						    loff_t nbytes);
static ssize_t (*orig_vfs_read)(struct file *file, char __user *buf,
				size_t count, loff_t *pos);
static ssize_t (*orig_vfs_write)(struct file *file, const char __user *buf,
				 size_t count, loff_t *pos);

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
 * See https://elixir.bootlin.com/linux/v5.1.5/source/include/linux/fs.h#L162
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

static inline loff_t nearest_left_page_boundary(loff_t offset)
{
	return offset - (offset & (PAGE_SIZE - 1));
}

static inline size_t nearest_right_page_boundary(size_t offset)
{
	size_t rem = (offset + PAGE_SIZE) & (PAGE_SIZE - 1);
	return rem == 0 ? offset : offset + PAGE_SIZE - rem;
}

/**
 * Return: %0 on success, negative error code otherwise
 */
static int advise_dontneed(unsigned int fd, loff_t fpos, size_t nbytes,
			   bool flush)
{
	loff_t offset;
	size_t len;

	if (flush) {
		int ret = orig_sys_sync_file_range2(
			fd, SYNC_FILE_RANGE_WRITE | SYNC_FILE_RANGE_WAIT_AFTER,
			fpos, nbytes);
		if (ret < 0)
			return ret;
	}

	/*
	 *  The offset and len must be aligned on a system page-sized boundary
	 *  in order to cover all dirty pages since partial page updates are
	 *  deliberately be ignored.
	 *  For sys_fadvise64():
	 *	https://elixir.bootlin.com/linux/v5.1.5/source/mm/fadvise.c#L120
	 *
	 *  Since we align the file offset and nbytes to the nearest page
	 *  boundary, when the size of buf or the value specified in count is
	 *  not suitably aligned, the actual bytes to be flushed may be more
	 *  than the number of bytes that the read()/write() returns, which
	 *  could result in performance degradation.
	 */
	offset = nearest_left_page_boundary(fpos);
	len = nearest_right_page_boundary(nbytes + fpos) - offset;

	return orig_sys_fadvise64_64(fd, offset, len, POSIX_FADV_DONTNEED);
}

/**
 * This function is improved based on io_is_direct() from
 * https://elixir.bootlin.com/linux/v5.1.5/source/include/linux/fs.h#L3297
 */
static inline bool is_direct(struct file *filp)
{
	return (filp->f_flags & O_DIRECT) || IS_DAX(filp->f_mapping->host) ||
	       IS_DAX(file_inode(filp));
}

static asmlinkage long no_fscache_sys_read(unsigned int fd, char __user *buf,
					   size_t count)
{
	struct fd f = cp_fdget_pos(fd);
	struct file *file = f.file;
	ssize_t ret = -EBADF;

	if (file) {
		loff_t pos = file_pos_read(file);
		umode_t i_mode = file_inode(file)->i_mode;

		ret = orig_vfs_read(file, buf, count, &pos);
		if (ret >= 0)
			file_pos_write(file, pos);
		cp_fdput_pos(f);

		if (ret >= 0 && S_ISREG(i_mode) && !is_direct(file))
			advise_dontneed(fd, file->f_pos - ret, ret, false);
	}
	return ret;
}

/**
 * See https://elixir.bootlin.com/linux/v5.1.5/source/include/linux/fs.h#L3321
 */
static inline bool is_sync(struct file *filp)
{
	return (filp->f_flags & O_DSYNC) || IS_SYNC(filp->f_mapping->host) ||
	       filp->f_flags & __O_SYNC;
}

static asmlinkage long
no_fscache_sys_write(unsigned int fd, const char __user *buf, size_t count)
{
	struct fd f = cp_fdget_pos(fd);
	struct file *file = f.file;
	ssize_t ret = -EBADF;

	if (file) {
		loff_t pos = file_pos_read(file);
		umode_t i_mode = file_inode(file)->i_mode;

		ret = orig_vfs_write(file, buf, count, &pos);
		if (ret >= 0)
			file_pos_write(file, pos);
		cp_fdput_pos(f);

		if (ret > 0 && S_ISREG(i_mode) && !is_direct(file))
			/*
			 * We don't need to flush the data if it is synced
			 * already.
			 */
			advise_dontneed(fd, file->f_pos - ret, ret,
					!is_sync(file));
	}

	return ret;
}

struct func_symbol {
	const char *name;
	void *func;
};

#define FUNC_SYMBOL(_name, _func)                                              \
	{                                                                      \
		.name = (_name), .func = (_func),                              \
	}

static struct func_symbol dept_fsyms[] = {
	FUNC_SYMBOL("sys_fadvise64_64", &orig_sys_fadvise64_64),
	FUNC_SYMBOL("sys_sync_file_range2", &orig_sys_sync_file_range2),
	FUNC_SYMBOL("vfs_read", &orig_vfs_read),
	FUNC_SYMBOL("vfs_write", &orig_vfs_write),
};

static int fill_func_symbol(struct func_symbol *fsym)
{
	unsigned long address = kallsyms_lookup_name(fsym->name);

	if (!address) {
		pr_err("unresolved symbol: %s\n", fsym->name);
		return -ENOENT;
	}

	*((unsigned long *)fsym->func) = address;

	return 0;
}

static int resolve_func(void)
{
	size_t count = ARRAY_SIZE(dept_fsyms);
	size_t i;

	for (i = 0; i < count; i++) {
		struct func_symbol *fsym = &dept_fsyms[i];
		int ret = fill_func_symbol(fsym);

		if (ret)
			return ret;
	}

	return 0;
}

#define KLP_FUNC(_old_name, _new_func)                                         \
	{                                                                      \
		.old_name = (_old_name), .new_func = (_new_func),              \
	}

static struct klp_func funcs[] = { KLP_FUNC("sys_read", no_fscache_sys_read),
				   KLP_FUNC("sys_write", no_fscache_sys_write),
				   {} };

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
