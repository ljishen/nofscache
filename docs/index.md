**nofscache is a loadable kernel moduel trying to eliminating page caching effects for user applications.**


## Requirements

- Only support 64-bit userspace programs since we didn't patch the compatibility version of system calls ([compat_sys_xyzzy()](https://www.kernel.org/doc/html/latest/process/adding-syscalls.html#compatibility-system-calls-generic)).


## Performance Results

<iframe width="857.51" height="370.84820333333334" seamless frameborder="0" scrolling="no" src="https://docs.google.com/spreadsheets/d/e/2PACX-1vTVNWUu5A_qmFfiO68-wHfQrb7jZeFr4U95_8CPBJhpkT4bxXRmSOSsPgCwfcfvs4LhGzySZ04It9dv/pubchart?oid=1781414827&amp;format=interactive"></iframe>

- For tests without module installed, we used the memory controller (`memory.limit_in_bytes`) in [cgroup](https://www.kernel.org/doc/Documentation/cgroup-v1/memory.txt) to limit the amount of memory (including page cache) can be used by the fio process. The limitations are generally set to a low number, and you can find the exact value we used for different tests in the section [Result Details](#result-details) below.
- Each number is the average of 5 data points in steady-state.
- Each test runs for 60 seconds. Here are the fio [job files](https://github.com/ljishen/nofscache/tree/master/tests/fio/jobs) of these tests.
- We used block size of 4KiB for sequential tests and 256KiB for random tests.

### Result Details

<iframe style="overflow:hidden;" width="100%" height="1400px" src="https://docs.google.com/spreadsheets/d/e/2PACX-1vTVNWUu5A_qmFfiO68-wHfQrb7jZeFr4U95_8CPBJhpkT4bxXRmSOSsPgCwfcfvs4LhGzySZ04It9dv/pubhtml?gid=1229428066&amp;single=true&amp;widget=true&amp;headers=false"></iframe>
