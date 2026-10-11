# Optional private GNU runtime

These recipes build a private GNU loader and libraries for use alongside
OpenWrt's musl system, without replacing the system libc. This directory contains
source recipes and patches; SDK/runtime binaries and inference applications are
outside its scope. No Python installation on the router is required.

## Source inputs

Use a separate AArch64 Linux build host and a **case-sensitive filesystem**.
The input source packages are:

* Ubuntu glibc `2.39-0ubuntu8.9` (all packaging patches, followed by 8 additional
  GNU/adapted patches).
* Ubuntu GCC `14.2.0-4ubuntu2~24.04.1` (the selected libstdc++ source projection,
  followed by GNU fixes; 11 additional patches in total).

Obtain exact source files from Ubuntu's source package archive, using authenticated
APT source metadata or the corresponding Launchpad source publication:
[glibc](https://launchpad.net/ubuntu/+source/glibc/2.39-0ubuntu8.9),
[gcc-14](https://launchpad.net/ubuntu/+source/gcc-14/14.2.0-4ubuntu2~24.04.1).
The filenames, lengths and SHA256 values are fixed in `sources.json`; the preparation
script rejects mismatches. No moving `latest` download is substituted.

```sh
python3 runtime/prepare.py glibc --downloads ./downloads/glibc --output ./build/glibc-source
python3 runtime/prepare.py libstdcxx --downloads ./downloads/gcc --output ./build/gcc-source
```

Python 3.12+ with tar data filters, GNU patch and normal Linux build prerequisites
are expected. Preparation runs no build and refuses an existing output directory.
It applies glibc's Debian series, then the additional series without fuzz. GCC's
Ubuntu archive contains an inner source tar; its hash is checked before applying
the recorded libstdc++ patch projection. That projection is not a full patched
GCC compiler distribution. Changed-file hashes are checked after patching.

## Build libraries into a local output directory

On the separate native AArch64 Linux host, install GNU build prerequisites
(including GCC/G++, make, binutils, bison, gawk and development headers). Use a
GCC 14 compiler for libstdc++:

```sh
python3 runtime/build.py glibc \
  --source ./build/glibc-source/glibc-2.39 --output ./build/glibc-output
CC=gcc-14 CXX=g++-14 python3 runtime/build.py libstdcxx \
  --source ./build/gcc-source/inner/gcc-14.2.0 --output ./build/libstdcxx-output
```

The script uses a dedicated installation prefix,
stack/branch protection and libstdc++ RELRO/bind-now settings. It verifies
AArch64 output; the libstdc++ configuration must enable POSIX memalign and
gthreads. The libraries are copied to the selected local output directory and
hashed. `make install`, global loader changes and device deployment are absent.
Byte-identical output across build environments is not guaranteed; compiler,
binutils, headers and build environment also affect output.

## Synthetic runtime checks

After both builds complete, run the new libraries through their private loader:

```sh
python3 runtime/check.py \
  --glibc-output ./build/glibc-output \
  --libstdcxx-output ./build/libstdcxx-output \
  --glibc-source ./build/glibc-source/glibc-2.39 \
  --output ./build/runtime-check
```

The check verifies output hashes and loader execution permission, then checks
which libc/libstdc++ are actually loaded. Synthetic programs cover the glibc
version, invalid `fopen` conversion modes, resolver initialization, the patched
path-normalization helper, C++ threads, aligned allocation and overflow rejection.
No DNS query, SDK, model or NPU access is performed. Supporting libraries and
compiler headers come from the build host; this is not validation of a complete
OpenWrt userspace closure or the entire upstream test suite.

The complete userspace closure additionally needs a separately reviewed
`libgcc_s.so.1`, an appropriately licensed RKNN SDK library and a compatible
application. This repository neither fetches nor redistributes them. Use
`readelf -d -V` to check needed libraries and symbol versions before assembling
that closure. Do not run `ldconfig` on OpenWrt or modify its default loader.

A private invocation has the following form after an operator has assembled and
verified the closure:

```sh
/usr/lib/r6s-rknn-runtime/lib/ld-linux-aarch64.so.1 \
  --library-path /usr/lib/r6s-rknn-runtime/lib /path/to/your-worker
```

Library checks do not validate SDK execution, tensor-layout handling or
application-level multi-core scheduling. Those require application-specific
testing with a compatible SDK and hardware.

## Patch attribution and limits

`sources.json` records GNU commits and adapted-backport origins. The fopen test
is registered for glibc 2.39; Power8 source changes are preserved but Power8
execution is not claimed. Loader path-normalization changes are adapted to
2.39's layout. GNU patch author headers and source licenses are retained.
See `THIRD_PARTY_NOTICES.md` and the license texts in `LICENSES/`.

These are a dated, pinned source set, not a promise of complete or current
security coverage. The closed SDK's internal code is outside this source review.
Existing build and hardware evidence is summarized in `docs/VALIDATION.md`.
