#!/usr/bin/env bash
#
# MiniJV880 build environment example.
#
# Copy this file to a local path outside the repository, or adapt it for
# your own machine. Do not commit personal absolute paths or local secrets.
#
# Example usage:
#
#   source /path/to/your/env-minijv880.sh
#   bash build.sh
#
# Required:
# - aarch64-none-elf toolchain in PATH
# - RPI set to the target Raspberry Pi model
#
# This project is currently developed/tested mainly with Raspberry Pi 4.

export PATH="/path/to/arm-gnu-toolchain/bin:$PATH"
export RPI=4
