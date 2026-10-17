# A "thread sanitizer" for the ReactOS kernel (KCSAN-style) - research and prototype

Date: 2026-10-17. Target: ReactOS amd64 (SMP kernel ntkrnlmp), GCC 8.4 from RosBE (`reactos-build` container).

## Why not ThreadSanitizer itself

- TSan (`-fsanitize=thread` + `libtsan`) is a user-mode runtime for Linux/macOS/FreeBSD. It has no
  Windows/mingw port and could never run in a kernel anyway (it reserves terabytes of shadow
  memory, intercepts pthreads, and tracks happens-before through every lock it knows about).
- Linux solved this for its kernel with **KCSAN** (Kernel Concurrency Sanitizer): it uses the
  *compiler half* of TSan (`-fsanitize=thread` puts a call `__tsan_read4(addr)` /
  `__tsan_write8(addr)` before every memory access) and replaces the runtime with a tiny one based
  on **soft watchpoints**. No shadow memory, no lock tracking, works at any IRQL. That is what fits
  ReactOS, and it is the same approach as our KASAN (compiler hooks + our own runtime in ntoskrnl).

## What the toolchain gives us (tested)

RosBE gcc 8.4.0 (`/opt/RosBE/amd64/bin/x86_64-w64-mingw32-gcc`) with `-fsanitize=thread -O2`:

- emits `__tsan_read1/2/4/8/16`, `__tsan_write1/2/4/8/16` (and `__tsan_unaligned_*` for packed
  data) before plain loads/stores;
- emits `__tsan_atomicN_*` for `__atomic_*` / `__sync_*` builtins. ReactOS' `Interlocked*`
  intrinsics (sdk/include/vcruntime/mingw32/intrin_x86.h) are `__sync_*` builtins, so **every
  Interlocked call in an instrumented file becomes a `__tsan_atomic*` call** - the runtime must
  implement them (it performs them with `__atomic_*` in an uninstrumented file);
- always emits `__tsan_func_entry/__tsan_func_exit` in every function (gcc 8 has no
  `--param tsan-instrument-func-entry-exit=0`; that and `--param tsan-distinguish-volatile=1`
  are gcc 11+ / clang). They are empty functions in our runtime (cost: 2 calls per function);
- adds a constructor calling `__tsan_init` per object file (.ctors); the kernel never runs it, we
  just provide an empty `__tsan_init`;
- does **not** turn `memcpy`/`memset`/`RtlCopyMemory` calls into `__tsan_read_range` (those
  library calls are unchecked); inline structure copies are checked;
- inline assembly (`__readgsqword`, spinlock asm, `KeGetCurrentIrql` via `mov cr8`) is never
  instrumented - good, that is how the PRCB stays out of it.

Without `tsan-distinguish-volatile`, volatile accesses look like plain ones. In ReactOS a lot of
intentionally racy code uses `volatile` fields (e.g. `KEVENT.Header.SignalState` read without the
dispatcher lock), so with gcc 8 those show up as reports. Two ways out: build the KCSAN files with
a newer gcc / clang (ReactOS has a clang toolchain file, `toolchain-clang.cmake`, which supports
`-fsanitize=thread -mllvm -tsan-distinguish-volatile -mllvm -tsan-instrument-func-entry-exit=0`),
or add a suppression list (PCs / functions) in the runtime.

## Runtime design (implemented in the prototype: ntoskrnl/kcsan/kcsan.c)

- **Watchpoint slots:** 64 slots, each a 64-bit word: `valid | consumed | write | size(8 bits) |
  address(low 48 bits)` (amd64 kernel addresses are canonical `0xFFFF8...`, so 48 bits are enough).
  Slot = `(address >> 4) % 64`; an access checks the slot of its first and last byte.
- **On every instrumented access** (`__tsan_readN/writeN`): if KCSAN is off, return. User-mode
  addresses are skipped (they are probed under SEH; the runtime must not fault there). Check the
  slots: a valid, unconsumed watchpoint that overlaps, with at least one of the two accesses a
  write, is a race. The checker claims it with `InterlockedCompareExchange64(... | consumed)` and
  stores its PC, thread, CPU, size and stack (stack only at IRQL <= APC_LEVEL: walking the stack
  can touch pageable unwind data, the same lesson as KASAN) into the slot.
- **Sampling:** a per-CPU countdown (randomized around `KcsanInterval`, default 64 accesses). When it
  reaches 0 and IRQL <= DISPATCH_LEVEL, the access becomes a watchpoint: claim a free slot, read the
  value (1/2/4/8 bytes), `KeStallExecutionProcessor(40 us)` (10 us at DISPATCH_LEVEL), read it again,
  `InterlockedExchange64(slot, 0)`. Consumed -> report both sides. Not consumed but the value
  changed -> report "changed by an uninstrumented access" (catches races with code that is not
  instrumented, e.g. a racing `memcpy` or a non-KCSAN module).
  The IRQL is never raised by the runtime: the original access follows at the same IRQL, so reading
  the address is exactly as safe as the access itself (pageable memory at PASSIVE_LEVEL is fine).
- **Where it is off:** above DISPATCH_LEVEL no watchpoint is set up (no stalls in interrupts or
  IPIs); checks still run (they only read the slot array). The runtime file itself is not
  instrumented, and everything it calls (HAL stall, RtlWalkFrameChain, DbgPrint, spinlocks) is in
  uninstrumented files, so there is no recursion and no per-CPU nesting counter is needed in the
  prototype. (If more of ntoskrnl gets instrumented - e.g. `rtl` or `ke` - a per-CPU "in KCSAN"
  flag in the PRCB becomes necessary.)
- **Marked accesses:** atomics (`__tsan_atomic*`, i.e. Interlocked*, `ReadNoFence`, ...) never set
  up or hit watchpoints - the Linux rule "plain vs marked". With gcc 11+/clang, `__tsan_volatile_*`
  are empty too, so `volatile` = "I know this races" (READ_ONCE-like).
- **Reports:** DbgPrint to the debug log (serial), deduplicated by (PC, other PC) pair, 128 entries.
  Format:
  ```
  KCSAN: data race: write of 8 bytes at FFFF... (pc ..., thread ..., cpu 1)
  KCSAN:   races with read of 8 bytes at FFFF... (pc ..., thread ..., cpu 0)
  KCSAN:   watcher stack: ...      KCSAN:   other stack: ...
  ```
  PCs/stack addresses are resolved with the usual tools (`scripts/symstacks.py`, `kptr_annotate.py`,
  or `addr2line` against the unstripped `ntkrnlmp.exe` / `.nostrip.exe`).
- **Counters** for the debugger: `KcsanWatchpoints`, `KcsanRaces`, `KcsanValueChanges`,
  `KcsanNoSlot`, `KcsanReports`; tunables `KcsanInterval`, `KcsanStallUs`, `KcsanStallUsDispatch`.
- **Enable:** boot option `KCSAN` (Options= line in freeldr.ini), parsed next to `KASAN` in
  ex/init.c. Without it every hook returns after one test of `KcsanEnabled`.

## Restricting it to selected code

Compile options per source file (directory-scoped, so it must be set where the target is defined;
ntos.cmake is included by both ntoskrnl/ and ntoskrnl/ntkrnlmp/):

```cmake
option(KCSAN_PROTOTYPE "..." OFF)
if(KCSAN_PROTOTYPE)
    set(KCSAN_FILES ntoskrnl/fsrtl/filelock.c ntoskrnl/cc/pin.c CACHE STRING "...")
    set_source_files_properties(${KCSAN_FILES} PROPERTIES
        COMPILE_OPTIONS "-fsanitize=thread" SKIP_PRECOMPILE_HEADERS ON)
    list(APPEND SOURCE ntoskrnl/kcsan/kcsan.c)
endif()
```

`SKIP_PRECOMPILE_HEADERS` because the ntoskrnl PCH was built without the flag. The RTL generic
table (the splay tree that was corrupted) lives in `sdk/lib/rtl`, which is a static library shared
with ntdll, so it cannot simply be instrumented; the prototype builds an extra static library
`rtl_kcsan` with instrumented copies of `generictable.c` and `splaytree.c` and links it before
`rtl` into ntoskrnl/ntkrnlmp (the linker then takes those objects from it).

For **drivers** (fastfat, etc.) it is the KASAN pattern: a `KCSAN_MODULES` list in the top
CMakeLists.txt doing `target_compile_options(${_module} PRIVATE -fsanitize=thread)` and exporting
the `__tsan_*` functions from ntoskrnl.spec (`@ cdecl __tsan_read4(ptr)` ...), the same way
`__asan_*_noabort` are exported now. fastfat is a good first candidate (allocsup.c/fatdata.c and its
FCB/VCB state, after tonight's FAT corruption).

## Self-test for the prototype

The prototype adds a test-only boot option `KCSANNOFLOCK` (implies KCSAN) that turns the new
`FsRtlpFileLockResource` (tonight's fix in fsrtl/filelock.c) off again. With it, the lockstress
test (repro/lockstress.c) on 2+ CPUs should produce KCSAN reports in filelock.c / generictable.c /
splaytree.c *before* the old RtlSplay crash. Without `KCSANNOFLOCK` (fix active) the same run should
be quiet on those files. That is the proof that the tool finds the bug class we care about.

## Cost

- Instrumented code: every load/store becomes a call (+ func entry/exit calls): 2-5x slower for
  those files with KCSAN off, far more with it on (a 40 us stall every ~64 accesses). Only selected
  files are instrumented, so the system as a whole stays usable; do not instrument ke/ or the
  dispatcher.
- Memory: 64 slots x ~170 bytes + small tables; no shadow memory.
- Detection is probabilistic: run stress tests long (minutes) on 2-4 CPUs. Lower `KcsanInterval`
  (e.g. 16) for small hot code like filelock.c, raise it for big code.

## Effort

| Step | Effort |
|---|---|
| Prototype runtime + ntoskrnl wiring (this, done) | ~2 h |
| First VM run with lockstress + KCSANNOFLOCK, tune interval/stall, triage false positives | 0.5-1 day |
| `KCSAN_MODULES` for drivers (spec exports, fastfat) | 2-3 h |
| Per-CPU nesting flag, suppressions file / `KCSAN_IGNORE` macro, symbolized one-line reports | 0.5 day |
| Clang build of the instrumented files for volatile/func-entry params, or newer RosBE gcc | 1 day (toolchain) |
| Broader coverage (cc/, mm/ selected files, io/iomgr): mostly triage of benign races | open-ended |

## Limitations of the prototype

- gcc 8: volatile accesses not distinguished (expect benign reports from flag polling);
  `__tsan_func_entry/exit` overhead can't be removed.
- Only races where *both* sides are instrumented are reported with two stacks; a race with
  uninstrumented code is seen only as "value changed" (and only if the value differs).
- Only kernel-space addresses; 1/2/4/8-byte values compared (16-byte and range accesses are only
  watched).
- Races on the same address through different virtual mappings (MDL mappings, cache views) aren't
  matched (Linux has the same limit).
- Not tested in a VM yet (no VM may be started now).

## Prototype: what was built (2026-10-17)

- Source: git worktree `~/reactos-win11/kcsan-src` (detached at f2ae8d44c46 + the uncommitted
  kernel changes of `~/reactos` copied over, incl. the filelock.c ERESOURCE fix). The main tree and
  build-amd64/build-kern are untouched.
- Patch with only the KCSAN changes (applies on top of the uncommitted filelock.c fix):
  `~/reactos-win11/patches/kcsan-prototype.patch` - new `ntoskrnl/kcsan/kcsan.c`; `ntos.cmake`
  (option `KCSAN_PROTOTYPE`, `KCSAN_FILES`); `ntoskrnl/CMakeLists.txt` (`rtl_kcsan` library) and
  `ntkrnlmp/CMakeLists.txt` (link it before `rtl`); `ex/init.c` (boot options `KCSAN`,
  `KCSANNOFLOCK`); `fsrtl/filelock.c` (the `KCSANNOFLOCK` self-test switch, `#ifdef KCSAN_PROTOTYPE`).
- Build: `REACTOS_SRC=~/reactos-win11/kcsan-src ARCH=amd64 BUILD_DIR=~/reactos-win11/build-kcsan WOW64=0
  CMAKE_EXTRA="-DKCSAN_PROTOTYPE=ON" BUILD_JOBS=3 scripts/build.sh --reconfigure ntkrnlmp`
  (log: build-kcsan-proto.log). Result: **`build-kcsan/ntoskrnl/ntkrnlmp/ntkrnlmp.exe` links**
  (24.5 MB with symbols, +~100 KB vs build-kern). Checked with nm/objdump: filelock.c, pin.c,
  generictable.c and splaytree.c call `__tsan_read*/write*` (pin.c also `__tsan_atomic32_fetch_add/sub`
  from its Interlocked counters, filelock.c `__tsan_read_range/write_range`), and the `RtlSplay` in
  the kernel is the instrumented copy (92 `__tsan` calls).
- Test (not run, no VM allowed now): `scripts/test-kcsan.sh` (copy of the installed amd64 disk,
  KCSAN kernel as ntoskrnl.exe, `/KCSAN` or `KCSAN_OPT=KCSANNOFLOCK`, 2 CPUs, runs
  `C:\lockstress.exe` and greps the serial log for `KCSAN`). Adjust the lockstress arguments to the
  final command line of repro/lockstress.c.
- Cleanup when done: `git -C ~/reactos worktree remove --force ~/reactos-win11/kcsan-src`.
