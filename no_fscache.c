// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
// Copyright (c) 2019, Jianshen Liu <jliu120@ucsc.edu>

/*
 * NOTE: 'readahead' is a module parameter that enables/disables the file
 *	 readahead behavior when calling open(), openat() and creat() system
 *	 calls. The default value is 'Y' (enabled).
 *
 *	 # To disable file readahead
 *	 echo 0 > /sys/module/no_fscache/parameters/readahead
 *
 *	 # To enable file readahead
 *	 echo 1 > /sys/module/no_fscache/parameters/readahead
 *
 * NOTE: 'no_fscache_device' is a module parameter that specifies which storage
 *	 devices are affected by this module. Multiple devices can be specified
 *	 by a comma-separated list. The default value is "" meaning no device
 *	 is affected initially.
 *
 *	 # To disable file system cache for storage device sda
 *	 echo 'sda' > /sys/module/no_fscache/parameters/no_fscache_device
 *
 *	 # To disable file system cache for storage devices sda and sdb
 *	 echo 'sda,sdb' > /sys/module/no_fscache/parameters/no_fscache_device
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/fadvise.h>
#include <linux/file.h>
#include <linux/fsnotify.h>
#include <linux/livepatch.h>
#include <linux/module.h>
#include <linux/sched/xacct.h>
#include <linux/uio.h>

static bool readahead = true;
static const struct kernel_param_ops readahead_param_ops = {
	.set = param_set_bint,
	.get = param_get_bool,
};
module_param_cb_unsafe(readahead, &readahead_param_ops, &readahead, 0644);
MODULE_PARM_DESC(readahead,
		 "Enable/Disable file readahead. Default: Y (enabled).");

/**
 * module_param_array_ops_named - renamed parameter which is an array of some
 * type.
 *
 * @name: a valid C identifier which is the parameter name
 * @array: the name of the array variable
 * @array_ops: the set & get operations for this array parameter
 * @type: the type, as per module_param()
 * @nump: optional pointer filled in with the number written
 * @perm: visibility in sysfs
 *
 * This exposes a different name than the actual variable name.  See
 * module_param_named() for why this might be necessary.
 */
#define module_param_array_ops_named(name, array, array_ops, type, nump, perm) \
	param_check_##type(name, &(array)[0]);                                 \
	static const struct kparam_array __param_arr_##name = {                \
		.max = ARRAY_SIZE(array),                                      \
		.num = nump,                                                   \
		.ops = &param_ops_##type,                                      \
		.elemsize = sizeof(array[0]),                                  \
		.elem = array                                                  \
	};                                                                     \
	__module_param_call(MODULE_PARAM_PREFIX, name, array_ops,              \
			    .arr = &__param_arr_##name, perm, -1,              \
			    KERNEL_PARAM_FL_UNSAFE);                           \
	__MODULE_PARM_TYPE(name, "array of " #type)

#ifdef CONFIG_SYSFS
/* Protects all built-in parameters, modules use their own param_lock */
static DEFINE_MUTEX(param_lock);

/* Use the module's mutex, or if built-in use the built-in mutex */
#ifdef CONFIG_MODULES
#define KPARAM_MUTEX(mod) ((mod) ? &(mod)->param_lock : &param_lock)
#else
#define KPARAM_MUTEX(mod) (&param_lock)
#endif

static inline void check_kparam_locked(struct module *mod)
{
	BUG_ON(!mutex_is_locked(KPARAM_MUTEX(mod)));
}
#else
static inline void check_kparam_locked(struct module *mod)
{
}
#endif /* !CONFIG_SYSFS */

/* We break the rule and mangle the string. */
static int param_array(struct module *mod, const char *name, const char *val,
		       unsigned int min, unsigned int max, void *elem,
		       int elemsize,
		       int (*set)(const char *, const struct kernel_param *kp),
		       s16 level, unsigned int *num)
{
	struct kernel_param kp;
	char save;

	/* Get the name right for errors. */
	kp.name = name;
	kp.arg = elem;
	kp.level = level;

	*num = 0;
	/*
	 * We expect a comma-separated list of values.
	 * Space around each element is ignored.
	 */
	do {
		int len;
		int ret;

		if (*num == max) {
			pr_err("%s: can only take %i arguments\n", name, max);
			return -EINVAL;
		}
		len = strcspn(val, ", ");

		/* nul-terminate and parse */
		save = val[len];

		if (len == 0) {
			val += 1;
			continue;
		}

		((char *)val)[len] = '\0';
		check_kparam_locked(mod);
		ret = set(val, &kp);

		if (ret != 0)
			return ret;
		kp.arg += elemsize;
		val += len + 1;
		(*num)++;
	} while (save == ',' || save == ' ');

	if (*num < min) {
		pr_err("%s: needs at least %i arguments\n", name, min);
		return -EINVAL;
	}
	return 0;
}

static int device_array_set(const char *val, const struct kernel_param *kp)
{
	const struct kparam_array *arr = kp->arr;
	unsigned int temp_num;

	return param_array(kp->mod, kp->name, val, 1, arr->max, arr->elem,
			   arr->elemsize, arr->ops->set, kp->level,
			   arr->num ?: &temp_num);
}

static int param_array_get(char *buffer, const struct kernel_param *kp)
{
	int i, off;
	const struct kparam_array *arr = kp->arr;
	struct kernel_param p = *kp;

	for (i = off = 0; i < (arr->num ? *arr->num : arr->max); i++) {
		int ret;

		/* Replace \n with comma */
		if (i)
			buffer[off - 1] = ',';
		p.arg = arr->elem + arr->elemsize * i;
		check_kparam_locked(p.mod);
		ret = arr->ops->get(buffer + off, &p);
		if (ret < 0)
			return ret;
		off += ret;
	}
	buffer[off] = '\0';
	return off;
}

static void param_array_free(void *arg)
{
	const struct kparam_array *arr = arg;

	if (arr->ops->free) {
		unsigned int i;

		for (i = 0; i < (arr->num ? *arr->num : arr->max); i++)
			arr->ops->free(arr->elem + arr->elemsize * i);
	}
}

#define MAX_BLKDEVS 128
static char *no_fscache_device_param[MAX_BLKDEVS];
static int count;
const struct kernel_param_ops device_array_ops = {
	.set = device_array_set,
	.get = param_array_get,
	.free = param_array_free,
};

module_param_array_ops_named(no_fscache_device, no_fscache_device_param,
			     &device_array_ops, charp, &count, 0644);

MODULE_PARM_DESC(no_fscache_device,
		 "The affected block devices. Default: \"\" (none).");

/*
 * These Functions are not exported to be used in loadable modules so we
 * look for them using kallsyms_lookup_name().
 */
static asmlinkage long (*orig_sys_fadvise64_64)(int fd, loff_t offset,
						size_t len, int advice);
static ssize_t (*orig_vfs_read)(struct file *file, char __user *buf,
				size_t count, loff_t *pos);
static ssize_t (*orig_vfs_write)(struct file *file, const char __user *buf,
				 size_t count, loff_t *pos);
static ssize_t (*orig_vfs_readv)(struct file *file,
				 const struct iovec __user *vec,
				 unsigned long vlen, loff_t *pos, rwf_t flags);
static int (*rw_verify_area)(int read_write, struct file *file,
			     const loff_t *ppos, size_t count);
static long (*orig_do_sys_open)(int dfd, const char __user *filename, int flags,
				umode_t mode);

static asmlinkage long no_fscache_sys_fadvise64_64(int fd, loff_t offset,
						   loff_t len, int advice)
{
	if (!readahead)
		switch (advice) {
		case POSIX_FADV_NORMAL:
		case POSIX_FADV_SEQUENTIAL:
		case POSIX_FADV_WILLNEED:
		case POSIX_FADV_NOREUSE:
			/*
			 * Implementation history of POSIX_FADV_RANDOM:
			 * https://lore.kernel.org/patchwork/patch/187024/
			 */
			advice = POSIX_FADV_RANDOM;
		}

	return orig_sys_fadvise64_64(fd, offset, len, advice);
}

static long no_fscache_do_sys_open(int dfd, const char __user *filename,
				   int flags, umode_t mode)
{
	/*
	 * We need O_DSYNC for the write operation as POSIX_FADV_DONTNEED uses
	 * WB_SYNC_NONE to writeback dirty pages which does not wait for
	 * existing IO to complete. See the following links for details:
	 *	https://elixir.bootlin.com/linux/v5.3/source/include/linux/writeback.h#L42
	 *	https://elixir.bootlin.com/linux/v5.3/source/mm/fadvise.c#L117
	 */
	int fd = orig_do_sys_open(dfd, filename, flags | O_DSYNC, mode);

	/*
	 * If no advice is given for an open file, the default assumption is
	 * POSIX_FADV_NORMAL, which sets the readahead window to the default
	 * size for the backing device.
	 * See https://linux.die.net/man/2/fadvise64_64
	 */
	if (!readahead) {
		/*
		 * Ignore return value because do_sys_open() shall return a
		 * file descriptor even if it fails to advice the access
		 * pattern.
		 */
		orig_sys_fadvise64_64(fd, 0, 0, POSIX_FADV_RANDOM);
	}

	return fd;
}

static unsigned long __orig_fdget_pos(unsigned int fd)
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

static inline struct fd orig_fdget_pos(int fd)
{
	return __to_fd(__orig_fdget_pos(fd));
}

/*
 * File is stream-like
 * See https://elixir.bootlin.com/linux/v5.3/source/include/linux/fs.h#L162
 */
#define FMODE_STREAM ((__force fmode_t)0x200000)

/* file_ppos returns &file->f_pos or NULL if file is stream */
static inline loff_t *file_ppos(struct file *file)
{
	return (file->f_mode & FMODE_STREAM) ? NULL : &file->f_pos;
}

static void __orig_f_unlock_pos(struct file *f)
{
	mutex_unlock(&f->f_pos_lock);
}

static inline void orig_fdput_pos(struct fd f)
{
	if (f.flags & FDPUT_POS_UNLOCK)
		__orig_f_unlock_pos(f.file);
	fdput(f);
}

/**
 * @fd: the file descriptor
 * @spos: start offset
 * @epos: end offset
 *
 * Return values are the same as the return values of fadvise64_64(2)
 */
static inline int do_advise_dontneed(unsigned int fd, loff_t spos, loff_t epos)
{
	loff_t startbyte;
	loff_t endbyte;

	/*
	 *  The offset and len must be aligned on a system page-sized boundary
	 *  in order to cover all dirty pages since partial page updates are
	 *  deliberately ignored.
	 *  For sys_fadvise64_64():
	 *	https://elixir.bootlin.com/linux/v5.3/source/mm/fadvise.c#L120
	 *
	 *  We align the file offset and nbytes to the nearest page
	 *  boundary. When the size of buf or the value specified in count for
	 *  read()/write() system call is not suitably aligned, the actual bytes
	 *  to be flushed will be greater than the number of bytes that
	 *  read()/write() returns. Therefore, we need to pay attention to
	 *  possible performance degradation because of this.
	 */

	startbyte = spos & PAGE_MASK;
	endbyte = (epos + (PAGE_SIZE - 1)) & PAGE_MASK;

	return orig_sys_fadvise64_64(fd, startbyte, endbyte - startbyte + 1,
				     POSIX_FADV_DONTNEED);
}

/*
 * This function is enhanced based on
 * io_is_direct() from
 *	https://elixir.bootlin.com/linux/v5.3/source/include/linux/fs.h#L3304
 * xfs_file_read_iter() from
 *	https://elixir.bootlin.com/linux/v5.3/source/fs/xfs/xfs_file.c#L260
 * and xfs_file_write_iter() from
 *	https://elixir.bootlin.com/linux/v5.3/source/fs/xfs/xfs_file.c#L705
 */
static inline bool is_direct(struct file *filp)
{
	return (filp->f_flags & O_DIRECT) || IS_DAX(filp->f_mapping->host) ||
	       IS_DAX(file_inode(filp));
}

/**
 * @ret: the return value from read/write system calls
 * @file: the struct file pointer of the file descriptor (%fd)
 * @fd: the file descriptor passed to read/write system calls
 * @pos: the end offset of bytes read/write in file.
 *	 System call
 *	 pread64()/pwrite64()/preadv()/pwritev()/preadv2()/pwritev2()
 *	 do not update the file offset at the end, so we can't get
 *	 this information through file->f_pos.
 */
static inline void advise_dontneed(ssize_t ret, struct file *file,
				   unsigned int fd, loff_t pos)
{
	umode_t i_mode = file_inode(file)->i_mode;

	if (ret >= 0 && S_ISREG(i_mode) && !is_direct(file))
		do_advise_dontneed(fd, pos - ret, pos);
}

static asmlinkage long no_fscache_sys_read(unsigned int fd, char __user *buf,
					   size_t count)
{
	struct fd f = orig_fdget_pos(fd);
	ssize_t ret = -EBADF;

	if (f.file) {
		loff_t pos, *ppos = file_ppos(f.file);

		if (ppos) {
			pos = *ppos;
			ppos = &pos;
		}
		ret = orig_vfs_read(f.file, buf, count, ppos);
		if (ret >= 0 && ppos)
			f.file->f_pos = pos;
		orig_fdput_pos(f);

		advise_dontneed(ret, f.file, fd, f.file->f_pos);
	}
	return ret;
}

static ssize_t do_readv(unsigned long fd, const struct iovec __user *vec,
			unsigned long vlen, rwf_t flags)
{
	struct fd f = orig_fdget_pos(fd);
	ssize_t ret = -EBADF;

	if (f.file) {
		loff_t pos, *ppos = file_ppos(f.file);

		if (ppos) {
			pos = *ppos;
			ppos = &pos;
		}
		ret = orig_vfs_readv(f.file, vec, vlen, ppos, flags);
		if (ret >= 0 && ppos)
			f.file->f_pos = pos;
		orig_fdput_pos(f);

		advise_dontneed(ret, f.file, fd, f.file->f_pos);
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
	ssize_t ret = -EBADF;

	if (pos < 0)
		return -EINVAL;

	f = fdget(fd);
	if (f.file) {
		ret = -ESPIPE;
		if (f.file->f_mode & FMODE_PREAD)
			ret = orig_vfs_read(f.file, buf, count, &pos);
		fdput(f);

		advise_dontneed(ret, f.file, fd, pos);
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
	ssize_t ret = -EBADF;

	if (pos < 0)
		return -EINVAL;

	f = fdget(fd);
	if (f.file) {
		ret = -ESPIPE;
		if (f.file->f_mode & FMODE_PREAD)
			ret = orig_vfs_readv(f.file, vec, vlen, &pos, flags);
		fdput(f);

		advise_dontneed(ret, f.file, fd, pos);
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

static asmlinkage long no_fscache_sys_preadv2(unsigned long fd,
					      const struct iovec __user *vec,
					      unsigned long vlen,
					      unsigned long pos_l,
					      unsigned long pos_h, rwf_t flags)
{
	loff_t pos = pos_from_hilo(pos_h, pos_l);

	if (pos == -1)
		return do_readv(fd, vec, vlen, flags);

	return do_preadv(fd, vec, vlen, pos, flags);
}

static asmlinkage long
no_fscache_sys_write(unsigned int fd, const char __user *buf, size_t count)
{
	struct fd f = orig_fdget_pos(fd);
	ssize_t ret = -EBADF;

	if (f.file) {
		loff_t pos, *ppos = file_ppos(f.file);

		if (ppos) {
			pos = *ppos;
			ppos = &pos;
		}
		ret = orig_vfs_write(f.file, buf, count, ppos);
		if (ret >= 0 && ppos)
			f.file->f_pos = pos;
		orig_fdput_pos(f);

		advise_dontneed(ret, f.file, fd, f.file->f_pos);
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
	kiocb.ki_pos = (ppos ? *ppos : 0);

	if (type == READ)
		ret = call_read_iter(filp, &kiocb, iter);
	else
		ret = call_write_iter(filp, &kiocb, iter);
	BUG_ON(ret == -EIOCBQUEUED);
	if (ppos)
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
	struct fd f = orig_fdget_pos(fd);
	ssize_t ret = -EBADF;

	if (f.file) {
		loff_t pos, *ppos = file_ppos(f.file);

		if (ppos) {
			pos = *ppos;
			ppos = &pos;
		}
		ret = vfs_writev(f.file, vec, vlen, ppos, flags);
		if (ret >= 0 && ppos)
			f.file->f_pos = pos;
		orig_fdput_pos(f);

		advise_dontneed(ret, f.file, fd, f.file->f_pos);
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
	ssize_t ret = -EBADF;

	if (pos < 0)
		return -EINVAL;

	f = fdget(fd);
	if (f.file) {
		ret = -ESPIPE;
		if (f.file->f_mode & FMODE_PWRITE)
			ret = orig_vfs_write(f.file, buf, count, &pos);
		fdput(f);

		advise_dontneed(ret, f.file, fd, pos);
	}

	return ret;
}

static ssize_t do_pwritev(unsigned long fd, const struct iovec __user *vec,
			  unsigned long vlen, loff_t pos, rwf_t flags)
{
	struct fd f;
	ssize_t ret = -EBADF;

	if (pos < 0)
		return -EINVAL;

	f = fdget(fd);
	if (f.file) {
		ret = -ESPIPE;
		if (f.file->f_mode & FMODE_PWRITE)
			ret = vfs_writev(f.file, vec, vlen, &pos, flags);
		fdput(f);

		advise_dontneed(ret, f.file, fd, pos);
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

static asmlinkage long no_fscache_sys_pwritev2(unsigned long fd,
					       const struct iovec __user *vec,
					       unsigned long vlen,
					       unsigned long pos_l,
					       unsigned long pos_h, rwf_t flags)
{
	loff_t pos = pos_from_hilo(pos_h, pos_l);

	if (pos == -1)
		return do_writev(fd, vec, vlen, flags);

	return do_pwritev(fd, vec, vlen, pos, flags);
}

struct func_symbol {
	const char *name;
	void *func;
	int skipmcount;
};

#define FUNC_SYMBOL(_name, _func, _skipmcount)                                 \
	{                                                                      \
		.name = (_name), .func = (_func), .skipmcount = (_skipmcount), \
	}

static struct func_symbol dept_fsyms[] = {
	FUNC_SYMBOL("sys_fadvise64_64", &orig_sys_fadvise64_64, 1),
	FUNC_SYMBOL("vfs_read", &orig_vfs_read, 0),
	FUNC_SYMBOL("vfs_write", &orig_vfs_write, 0),
	FUNC_SYMBOL("vfs_readv", &orig_vfs_readv, 0),
	FUNC_SYMBOL("rw_verify_area", &rw_verify_area, 0),
	FUNC_SYMBOL("do_sys_open", &orig_do_sys_open, 1),
};

static int fill_func_symbol(struct func_symbol *fsym)
{
	unsigned long address = kallsyms_lookup_name(fsym->name);

	if (!address) {
		pr_err("unresolved symbol: %s\n", fsym->name);
		return -ENOENT;
	}

	*((unsigned long *)fsym->func) = address;
	if (fsym->skipmcount)
		*((unsigned long *)fsym->func) += MCOUNT_INSN_SIZE;

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
	KLP_FUNC("sys_fadvise64_64", no_fscache_sys_fadvise64_64),
	KLP_FUNC("sys_read", no_fscache_sys_read),
	KLP_FUNC("sys_write", no_fscache_sys_write),
	KLP_FUNC("sys_readv", no_fscache_sys_readv),
	KLP_FUNC("sys_writev", no_fscache_sys_writev),
	KLP_FUNC("sys_pread64", no_fscache_sys_pread64),
	KLP_FUNC("sys_pwrite64", no_fscache_sys_pwrite64),
	KLP_FUNC("sys_preadv", no_fscache_sys_preadv),
	KLP_FUNC("sys_pwritev", no_fscache_sys_pwritev),
	KLP_FUNC("sys_preadv2", no_fscache_sys_preadv2),
	KLP_FUNC("sys_pwritev2", no_fscache_sys_pwritev2),
	KLP_FUNC("do_sys_open", no_fscache_do_sys_open),
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

MODULE_DESCRIPTION("Bypass the file system read and write caches");
MODULE_VERSION("0.1");
MODULE_AUTHOR("Jianshen Liu <jliu120@ucsc.edu>");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_INFO(livepatch, "Y");
