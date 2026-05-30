# Building from source

This page documents the currently validated local build setup for the
MiniJV880-CardRAM public source snapshot.

## Recommended toolchain

For Raspberry Pi 4 / AArch64 builds, use:

    Arm GNU Toolchain 13.2.Rel1
    aarch64-none-elf-gcc 13.2.1

This is the toolchain used for the validated public `v2.4.0-public-clean.7`
kernel-only package.

## Known incompatible toolchain

Arm GNU Toolchain 15.2.Rel1 / `aarch64-none-elf-gcc 15.2.1` is currently not
recommended for this source tree.

With the current recorded Circle/newlib submodules, GCC 15.2.Rel1 fails while
building `circle-newlib/newlib/libm/complex`, with errors such as implicit
declarations of long double math functions including `logl`, `atan2l`, `coshl`,
`sinhl`, `powl`, `fabsl`, and `copysignl`.

This is a dependency/toolchain compatibility issue in the Circle/newlib build
stack, not a MiniJV880 application source error.

## Initialize submodules

After cloning the repository, initialize the public submodules:

    git submodule update --init --recursive

## Check the active compiler

Before building, verify that the intended compiler is first in `PATH`:

    command -v aarch64-none-elf-gcc
    aarch64-none-elf-gcc --version | head -n 3

For the validated setup, the version should report GCC 13.2.1 from Arm GNU
Toolchain 13.2.Rel1.

## Example Raspberry Pi 4 build

Adapt the path to match your local installation:

    export PATH="$HOME/opt/toolchains/arm-gnu-toolchain-13.2.Rel1-x86_64-aarch64-none-elf/bin:$PATH"
    RPI=4 bash build.sh

The expected kernel output is:

    src/kernel8-rpi4.img

`build.sh` also copies the image to `releases/` using the project version.

## Kernel-only package helper

After a successful public build, a kernel-only package can be created with:

    tools/make_public_kernel_package.sh \
      --kernel src/kernel8-rpi4.img \
      --kernel-provenance "Built from this public source tree using Arm GNU Toolchain 13.2.Rel1."

The generated package intentionally contains only the compiled kernel image,
`README-SD-ROOT.txt`, and `SHA256SUMS.txt`. It does not include Raspberry Pi
boot firmware, Roland data, personal configuration files, ROMs, CardRAM content,
SysEx banks, logs, or Wi-Fi credentials.
