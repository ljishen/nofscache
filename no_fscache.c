// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
// Copyright (c) 2019, Jianshen Liu

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/fadvise.h>
#include <linux/file.h>
#include <linux/fsnotify.h>
#include <linux/kernel.h>
#include <linux/livepatch.h>
#include <linux/module.h>
#include <linux/sched/xacct.h>
#include <linux/uio.h>

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
static ssize_t (*orig_vfs_readv)(struct file *file,
				 const struct iovec __user *vec,
				 unsigned long vlen, loff_t *pos, rwf_t flags);
static int (*rw_verify_area)(int read_write, struct file *file,
			     const loff_t *ppos, size_t count);

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
 * See https://elixir.bootlin.com/linux/v5.1.6/source/include/linux/fs.h#L162
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

static inline void advise_dontneed_after_read(ssize_t ret, struct file *file,
					      unsigned int fd, loff_t lpos)
{
	umode_t i_mode = file_inode(file)->i_mode;

	if (ret >= 0 && S_ISREG(i_mode) && !is_direct(file))
		advise_dontneed(fd, lpos - ret, ret, false);
}

static asmlinkage long no_fscache_sys_read(unsigned int fd, char __user *buf,
					   size_t count)
{
	struct fd f = cp_fdget_pos(fd);
	struct file *file = f.file;
	ssize_t ret = -EBADF;

	if (file) {
		loff_t pos = file_pos_read(file);

		ret = orig_vfs_read(file, buf, count, &pos);
		if (ret >= 0)
			file_pos_write(file, pos);
		cp_fdput_pos(f);

		advise_dontneed_after_read(ret, file, fd, file->f_pos);
	}
	return ret;
}

static ssize_t do_readv(unsigned long fd, const struct iovec __user *vec,
			unsigned long vlen, rwf_t flags)
{
	struct fd f = cp_fdget_pos(fd);
	struct file *file = f.file;
	ssize_t ret = -EBADF;

	if (file) {
		loff_t pos = file_pos_read(file);

		ret = orig_vfs_readv(file, vec, vlen, &pos, flags);
		if (ret >= 0)
			file_pos_write(file, pos);
		cp_fdput_pos(f);

		advise_dontneed_after_read(ret, file, fd, file->f_pos);
	}

	if (ret > 0)
		add_rchar(current, ret);
	inc_syscr(current);
	return ret;
}

static asmlinkage long no_fscache_sys_readv(unsigned long fd,
					    const struct iovec __user *vec,
					    unsigned long vlen)
{
	return do_readv(fd, vec, vlen, 0);
}

static asmlinkage long no_fscache_sys_pread64(unsigned int fd, char __user *buf,
					      size_t count, loff_t pos)
{
	struct fd f;
	struct file *file;
	ssize_t ret = -EBADF;

	if (pos < 0)
		return -EINVAL;

	f = fdget(fd);
	file = f.file;

	if (file) {
		ret = -ESPIPE;
		if (file->f_mode & FMODE_PREAD)
			ret = orig_vfs_read(file, buf, count, &pos);
		fdput(f);

		advise_dontneed_after_read(ret, file, fd, pos);
	}

	return ret;
}

static inline loff_t pos_from_hilo(unsigned long high, unsigned long low)
{
#define HALF_LONG_BITS (BITS_PER_LONG / 2)
	return (((loff_t)high << HALF_LONG_BITS) << HALF_LONG_BITS) | low;
}

static ssize_t do_preadv(unsigned long fd, const struct iovec __user *vec,
			 unsigned long vlen, loff_t pos, rwf_t flags)
{
	struct fd f;
	struct file *file;
	ssize_t ret = -EBADF;

	if (pos < 0)
		return -EINVAL;

	f = fdget(fd);
	file = f.file;

	if (file) {
		ret = -ESPIPE;
		if (file->f_mode & FMODE_PREAD)
			ret = orig_vfs_readv(file, vec, vlen, &pos, flags);
		fdput(f);

		advise_dontneed_after_read(ret, file, fd, pos);
	}

	if (ret > 0)
		add_rchar(current, ret);
	inc_syscr(current);
	return ret;
}

static asmlinkage long no_fscache_sys_preadv(unsigned long fd,
					     const struct iovec __user *vec,
					     unsigned long vlen,
					     unsigned long pos_l,
					     unsigned long pos_h)
{
	loff_t pos = pos_from_hilo(pos_h, pos_l);

	return do_preadv(fd, vec, vlen, pos, 0);
}

/**
 * See https://elixir.bootlin.com/linux/v5.1.5/source/include/linux/fs.h#L3321
 */
static inline bool is_sync(struct file *filp)
{
	return (filp->f_flags & O_DSYNC) || IS_SYNC(filp->f_mapping->host) ||
	       filp->f_flags & __O_SYNC;
}

static inline void advise_dontneed_after_write(ssize_t ret, struct file *file,
					       unsigned int fd, loff_t pos)
{
	umode_t i_mode = file_inode(file)->i_mode;

	if (ret > 0 && S_ISREG(i_mode) && !is_direct(file))
		/*
		 * We don't need to flush the data if it is synced
		 * already.
		 */
		advise_dontneed(fd, pos - ret, ret, !is_sync(file));
}

static asmlinkage long
no_fscache_sys_write(unsigned int fd, const char __user *buf, size_t count)
{
	struct fd f = cp_fdget_pos(fd);
	struct file *file = f.file;
	ssize_t ret = -EBADF;

	if (file) {
		loff_t pos = file_pos_read(file);

		ret = orig_vfs_write(file, buf, count, &pos);
		if (ret >= 0)
			file_pos_write(file, pos);
		cp_fdput_pos(f);

		advise_dontneed_after_write(ret, file, fd, file->f_pos);
	}

	return ret;
}

static ssize_t do_iter_readv_writev(struct file *filp, struct iov_iter *iter,
				    loff_t *ppos, int type, rwf_t flags)
{
	struct kiocb kiocb;
	ssize_t ret;

	init_sync_kiocb(&kiocb, filp);
	ret = kiocb_set_rw_flags(&kiocb, flags);
	if (ret)
		return ret;
	kiocb.ki_pos = *ppos;

	if (type == READ)
		ret = call_read_iter(filp, &kiocb, iter);
	else
		ret = call_write_iter(filp, &kiocb, iter);
	BUG_ON(ret == -EIOCBQUEUED);
	*ppos = kiocb.ki_pos;
	return ret;
}

/* Do it by hand, with file-ops */
static ssize_t do_loop_readv_writev(struct file *filp, struct iov_iter *iter,
				    loff_t *ppos, int type, rwf_t flags)
{
	ssize_t ret = 0;

	if (flags & ~RWF_HIPRI)
		return -EOPNOTSUPP;

	while (iov_iter_count(iter)) {
		struct iovec iovec = iov_iter_iovec(iter);
		ssize_t nr;

		if (type == READ) {
			nr = filp->f_op->read(filp, iovec.iov_base,
					      iovec.iov_len, ppos);
		} else {
			nr = filp->f_op->write(filp, iovec.iov_base,
					       iovec.iov_len, ppos);
		}

		if (nr < 0) {
			if (!ret)
				ret = nr;
			break;
		}
		ret += nr;
		if (nr != iovec.iov_len)
			break;
		iov_iter_advance(iter, nr);
	}

	return ret;
}

static ssize_t do_iter_write(struct file *file, struct iov_iter *iter,
			     loff_t *pos, rwf_t flags)
{
	size_t tot_len;
	ssize_t ret = 0;

	if (!(file->f_mode & FMODE_WRITE))
		return -EBADF;
	if (!(file->f_mode & FMODE_CAN_WRITE))
		return -EINVAL;

	tot_len = iov_iter_count(iter);
	if (!tot_len)
		return 0;
	ret = rw_verify_area(WRITE, file, pos, tot_len);
	if (ret < 0)
		return ret;

	if (file->f_op->write_iter)
		ret = do_iter_readv_writev(file, iter, pos, WRITE, flags);
	else
		ret = do_loop_readv_writev(file, iter, pos, WRITE, flags);
	if (ret > 0)
		fsnotify_modify(file);
	return ret;
}

static ssize_t vfs_writev(struct file *file, const struct iovec __user *vec,
			  unsigned long vlen, loff_t *pos, rwf_t flags)
{
	struct iovec iovstack[UIO_FASTIOV];
	struct iovec *iov = iovstack;
	struct iov_iter iter;
	ssize_t ret;

	ret = import_iovec(WRITE, vec, vlen, ARRAY_SIZE(iovstack), &iov, &iter);
	if (ret >= 0) {
		file_start_write(file);
		ret = do_iter_write(file, &iter, pos, flags);
		file_end_write(file);
		kfree(iov);
	}
	return ret;
}

static ssize_t do_writev(unsigned long fd, const struct iovec __user *vec,
			 unsigned long vlen, rwf_t flags)
{
	struct fd f = cp_fdget_pos(fd);
	struct file *file = f.file;
	ssize_t ret = -EBADF;

	if (file) {
		loff_t pos = file_pos_read(file);

		ret = vfs_writev(file, vec, vlen, &pos, flags);
		if (ret >= 0)
			file_pos_write(file, pos);
		cp_fdput_pos(f);

		advise_dontneed_after_write(ret, file, fd, file->f_pos);
	}

	if (ret > 0)
		add_wchar(current, ret);
	inc_syscw(current);
	return ret;
}

static asmlinkage long no_fscache_sys_writev(unsigned long fd,
					     const struct iovec __user *vec,
					     unsigned long vlen)
{
	return do_writev(fd, vec, vlen, 0);
}

static asmlinkage long no_fscache_sys_pwrite64(unsigned int fd,
					       const char __user *buf,
					       size_t count, loff_t pos)
{
	struct fd f;
	struct file *file;
	ssize_t ret = -EBADF;

	if (pos < 0)
		return -EINVAL;

	f = fdget(fd);
	file = f.file;

	if (file) {
		ret = -ESPIPE;
		if (file->f_mode & FMODE_PWRITE)
			ret = orig_vfs_write(file, buf, count, &pos);
		fdput(f);

		advise_dontneed_after_write(ret, file, fd, pos);
	}

	return ret;
}

static ssize_t do_pwritev(unsigned long fd, const struct iovec __user *vec,
			  unsigned long vlen, loff_t pos, rwf_t flags)
{
	struct fd f;
	struct file *file;
	ssize_t ret = -EBADF;

	if (pos < 0)
		return -EINVAL;

	f = fdget(fd);
	file = f.file;

	if (file) {
		ret = -ESPIPE;
		if (file->f_mode & FMODE_PWRITE)
			ret = vfs_writev(file, vec, vlen, &pos, flags);
		fdput(f);

		advise_dontneed_after_write(ret, file, fd, pos);
	}

	if (ret > 0)
		add_wchar(current, ret);
	inc_syscw(current);
	return ret;
}

static asmlinkage long no_fscache_sys_pwritev(unsigned long fd,
					      const struct iovec __user *vec,
					      unsigned long vlen,
					      unsigned long pos_l,
					      unsigned long pos_h)
{
	loff_t pos = pos_from_hilo(pos_h, pos_l);

	return do_pwritev(fd, vec, vlen, pos, 0);
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
	FUNC_SYMBOL("vfs_readv", &orig_vfs_readv),
	FUNC_SYMBOL("rw_verify_area", &rw_verify_area),
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

static struct klp_func funcs[] = {
	KLP_FUNC("sys_read", no_fscache_sys_read),
	KLP_FUNC("sys_write", no_fscache_sys_write),
	KLP_FUNC("sys_readv", no_fscache_sys_readv),
	KLP_FUNC("sys_writev", no_fscache_sys_writev),
	KLP_FUNC("sys_pread64", no_fscache_sys_pread64),
	KLP_FUNC("sys_pwrite64", no_fscache_sys_pwrite64),
	KLP_FUNC("sys_preadv", no_fscache_sys_preadv),
	KLP_FUNC("sys_pwritev", no_fscache_sys_pwritev),
	{}
};

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
