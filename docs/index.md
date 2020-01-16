**nofscache is a loadable kernel moduel used to to eliminating page caching effects for user applications.**


## Requirements

- Linux kernel version >= 4.12
- Having `CONFIG_ADVISE_SYSCALLS=y` in /boot/config-\$(shell uname -r)
- This module affects only 64-bit userspace programs since we didn't patch the compatibility version of system calls ([compat_sys_xyzzy()](https://www.kernel.org/doc/html/latest/process/adding-syscalls.html#compatibility-system-calls-generic)).


## Installation

```bash
git clone https://github.com/ljishen/nofscache.git
cd nofscache
make install
```

If you keep seeing processes under

```bash
[INFO] Checking transition state for module no_fscache (update every 2s)...

[INFO] 2 tasks are not in patched state:
USER       PID   TID CMD
root      1040  1131 /usr/bin/lxcfs /var/lib/lxcfs/
root      1040  1133 /usr/bin/lxcfs /var/lib/lxcfs/

...
```

You may need to manually kill these processes because they are stopping the module from finishing the transition state from `unpatched` to `patched`. See [livepatch consistency model](https://www.kernel.org/doc/Documentation/livepatch/livepatch.txt) for more details.

To uninstall the kernel module, use

```bash
make uninstall
```

Again, you may need to kill processes to help the module finish the transition state from `patched` to `unpatched`.


## Limitations

There are four basic Linux I/O models,

![Four Linux IO Models](https://user-images.githubusercontent.com/468515/70580588-34d34b00-1b69-11ea-93bf-1f33acf78d31.png)

This module fully supports synchronous blocking I/O, and implicitly supports synchronous non-blocking I/O and asynchronous blocking I/O. For asynchronous non-blocking I/O, this module supports POSIX AIO as it is a user-space implementation that actually calls blocking I/O interfaces. The module may support [libaio](https://pagure.io/libaio) as it mainly focuses on direct I/O. We hope to support the latest I/O interface, io_uring, but this module is not ready yet. Here is [a really good article](https://developer.ibm.com/articles/l-async/) explaining the differences between these I/O models.


## Performance Results

![Performance Results of Module no_fscache](https://user-images.githubusercontent.com/468515/70580586-343ab480-1b69-11ea-88a6-4cbbf8b37804.png)

- For tests without module installed, we used the memory controller (`memory.limit_in_bytes`) in [cgroup](https://www.kernel.org/doc/Documentation/cgroup-v1/memory.txt) to limit the amount of memory (including page cache) can be used by the fio process. The limitations are generally set to a low number, and you can find the exact value we used for different tests in the section [Result Details](#result-details) below.
- Each number is the average of 5 data points in steady-state.
- Each test runs for 60 seconds. Here are the fio [job files](https://github.com/ljishen/nofscache/tree/master/tests/fio/jobs) of all tests.
- We used block size of 4KiB for sequential tests and 256KiB for random tests.

### Result Details

<iframe scrolling="no" style="overflow:hidden" width="100%" height="1520px" src="https://docs.google.com/spreadsheets/d/e/2PACX-1vTVNWUu5A_qmFfiO68-wHfQrb7jZeFr4U95_8CPBJhpkT4bxXRmSOSsPgCwfcfvs4LhGzySZ04It9dv/pubhtml?gid=1229428066&amp;single=true&amp;widget=true&amp;headers=false"></iframe>


## References

1. Design Considerations of Eliminating External Caching Effects for MBWU Construction<br/>
   [https://cross.ucsc.edu/news/blog/mbwuconstruction_122010.html](https://cross.ucsc.edu/news/blog/mbwuconstruction_122010.html)

2. Implementing a Kernel Module to Eliminating External Caching Effects<br/>
   [https://cross.ucsc.edu/news/blog/mbwuconstruction_012020.html](https://cross.ucsc.edu/news/blog/mbwuconstruction_012020.html)
