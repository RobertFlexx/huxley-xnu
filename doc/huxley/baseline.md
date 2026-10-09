# Repository baseline

Investigation began on 2026-10-08 with a clean `main` branch tracking
`origin/main`, at `05873c6ae3a7214268538a24e77c53c97ec49ddc`.
The sole configured remote is `https://github.com/RobertFlexx/huxley-xnu`.

The imported source commit is `f6217f891ac0bb64f3d375211650a4c1ff8ca1ea`,
whose commit subject is `xnu-12377.1.9`. It is an ancestor of HEAD. No upstream
remote or release tag is configured locally; this identifies the local import
exactly, rather than claiming a new fetch or tag verification. Comparison of
that commit with initial HEAD changes only the README: the Huxley description
replaced the upstream README, then was renamed to `README.txt`. There were no
preexisting kernel modifications or uncommitted changes to discard.

`makedefs/MakeInc.def` supports X86_64, X86_64H and ARM64, and RELEASE,
DEVELOPMENT, DEBUG, PROFILE, KASAN and SPTM configurations. X86 machines use
NONE. ARM64 includes Apple SoC configurations and VMAPPLE, not QEMU `virt`.
Source architecture support is not evidence of a working platform.

The untouched baseline was invoked with:

```sh
make -j2 all ARCH_CONFIGS=X86_64 KERNEL_CONFIGS=DEVELOPMENT \
  OBJROOT="$PWD/BUILD/huxley/baseline/obj" \
  SYMROOT="$PWD/BUILD/huxley/baseline/sym" \
  DSTROOT="$PWD/BUILD/huxley/baseline/dst"
```

It exited 2 before compilation. Missing `/usr/bin/xcrun`, `sw_vers` and Darwin
`sysctl hw.memsize` discovery led to an empty parallel-build count and
`makedefs/MakeInc.cmd:567: invalid second argument to 'wordlist' function`.
The local evidence is `BUILD/huxley/baseline/make.log` and `result.json`.

The developer build command supplies real host memory/CPU sizes and a single
kernel build stripe without changing upstream Makefiles. It advances to
`MakeInc.kernel:299`, which cannot obtain the Darwin kernel version from a
missing SDK/KDK. SDK/tool discovery still fails. This is an executed failed
build with missing prerequisites, not a successful kernel compilation.

Host: Linux x86_64, PikaOS kernel `7.2.6-pikaos`; Clang/LLD 23.1.0,
GCC 16.2.0, GNU Make 4.4.1, Python 3.14.7, QEMU 11.0.2. OVMF 4 MiB firmware
is installed. No xcrun, Xcode or MIG is available. The installed Clang lacks
its ASan runtime; GCC provides the sanitizer runtime used in the host tests.

Existing tests include Darwin userspace tests in `tests/`, SDK-dependent host
unit tests in `tests/unit/`, in-kernel tests in `osfmk/tests/`, and LLDB macro
tests under `tools/lldbmacros/tests/`. They were retained. The platform tests
added here run without those Apple SDK/test-framework dependencies.

The upstream license notices, `APPLE_LICENSE`, imported history and
`.upstream_base_commits` remain intact. New BSD-licensed files are identified
by their SPDX notices and `LICENSE.huxley`.
