**nofscache is a loadable kernel moduel trying to eliminating page caching effects for user applications.**

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

<style type="text/css">
.tg  {border-collapse:collapse;border-spacing:0;border-width:1px;border-style:solid;border-color:#ccc;}
.tg td{font-family:Arial, sans-serif;font-size:14px;padding:10px 5px;border-style:solid;border-width:0px;overflow:hidden;word-break:normal;border-color:#ccc;color:#333;background-color:#fff;}
.tg th{font-family:Arial, sans-serif;font-size:14px;font-weight:normal;padding:10px 5px;border-style:solid;border-width:0px;overflow:hidden;word-break:normal;border-color:#ccc;color:#333;background-color:#f0f0f0;}
.tg .tg-r02c{font-family:"Courier New", Courier, monospace !important;;background-color:#ffffff00;border-color:#ffffff00;text-align:center;vertical-align:middle}
.tg .tg-jlnk{background-color:#ffffff00;font-weight:bold;font-family:"Courier New", Courier, monospace !important;;color:#ffffff;border-color:#ffffff00;text-align:center;vertical-align:middle}
.tg .tg-qbe5{font-weight:bold;font-family:"Courier New", Courier, monospace !important;;background-color:#ffccc9;border-color:#ffffff00;text-align:center;vertical-align:middle}
.tg .tg-byox{font-weight:bold;font-family:"Courier New", Courier, monospace !important;;background-color:#ffffff00;color:#ffffff;border-color:#ffffff00;text-align:center;vertical-align:middle}
.tg .tg-94pa{background-color:#5fbb7d;font-weight:bold;font-family:"Courier New", Courier, monospace !important;;border-color:#ffffff00;text-align:center;vertical-align:middle}
.tg .tg-p99n{font-weight:bold;font-family:"Courier New", Courier, monospace !important;;background-color:#5fbb7d;border-color:#ffffff00;text-align:center;vertical-align:middle}
</style>
<table class="tg">
  <tr>
    <th class="tg-r02c"></th>
    <th class="tg-byox">Blocking</th>
    <th class="tg-byox">Non-blocking</th>
  </tr>
  <tr>
    <td class="tg-jlnk">Synchronous</td>
    <td class="tg-94pa">read/write</td>
    <td class="tg-94pa">read/write<br>(O_NONBLOCK)</td>
  </tr>
  <tr>
    <td class="tg-byox">Asynchronous</td>
    <td class="tg-p99n">I/O multiplexing<br>(select/poll) </td>
    <td class="tg-qbe5">AIO<br>(libaio/io_uring)</td>
  </tr>
</table>

This module supports synchronous blocking I/O, synchronous non-blocking I/O and asynchronous blocking I/O. It has no effect on asynchronous non-blocking I/O. Here is [a really good article](https://developer.ibm.com/articles/l-async/) explaining the differences between these I/O models.

## Performance Results

<iframe width="857.51" height="370.84820333333334" seamless frameborder="0" scrolling="no" src="https://docs.google.com/spreadsheets/d/e/2PACX-1vTVNWUu5A_qmFfiO68-wHfQrb7jZeFr4U95_8CPBJhpkT4bxXRmSOSsPgCwfcfvs4LhGzySZ04It9dv/pubchart?oid=1781414827&amp;format=interactive"></iframe>

- For tests without module installed, we used the memory controller (`memory.limit_in_bytes`) in [cgroup](https://www.kernel.org/doc/Documentation/cgroup-v1/memory.txt) to limit the amount of memory (including page cache) can be used by the fio process. The limitations are generally set to a low number, and you can find the exact value we used for different tests in the section [Result Details](#result-details) below.
- Each number is the average of 5 data points in steady-state.
- Each test runs for 60 seconds. Here are the fio [job files](https://github.com/ljishen/nofscache/tree/master/tests/fio/jobs) of these tests.
- We used block size of 4KiB for sequential tests and 256KiB for random tests.

### Result Details

<iframe scrolling="no" style="overflow:hidden" width="100%" height="1480px" src="https://docs.google.com/spreadsheets/d/e/2PACX-1vTVNWUu5A_qmFfiO68-wHfQrb7jZeFr4U95_8CPBJhpkT4bxXRmSOSsPgCwfcfvs4LhGzySZ04It9dv/pubhtml?gid=1229428066&amp;single=true&amp;widget=true&amp;headers=false"></iframe>
