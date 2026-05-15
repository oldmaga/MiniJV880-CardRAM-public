# Attributions

MiniJV880-CardRAM is based on work from several upstream projects.

## Upstream projects

- Mini-JV880 / MiniJV880 by giulioz  
  https://github.com/giulioz/mini-jv880

- Mini-JV880pi by Sterr1  
  https://github.com/Sterr1/Mini-JV880pi

- Early Mini-JV880 modifications by plamikcho  
  https://github.com/plamikcho/mini-jv880

- Nuked-SC55 by nukeykt  
  https://github.com/nukeykt/Nuked-SC55

- MiniDexed by probonopd and contributors  
  https://github.com/probonopd/MiniDexed

- Circle by rsta2  
  https://github.com/rsta2/circle

- circle-stdlib by smuehlst  
  https://github.com/smuehlst/circle-stdlib

- CMSIS_5 by Arm  
  https://github.com/ARM-software/CMSIS_5

## MiniJV880-CardRAM changes

This public-clean snapshot is based on the private MiniJV880-CardRAM v2.4.0
development state.

Main changes in this line include:

- DATA long press support for native Card/C bank selection.
- DATA short press preserved for SR overlay access.
- Card/C selection tested in Patch Play, Performance Play, Patch Write,
  Performance Write, Patch Copy and Performance Copy.
- MONITOR no longer used as a Card/C fallback.
- CardRAM probe wrap fix for Patch Play navigation.
- HTTP/TFTP maintenance work.
- PC-side CardRAM management tools and GUI.

## Material not included

This repository intentionally does not include:

- Roland ROMs.
- SR-JV80 ROMs.
- SysEx sound banks.
- CardRAM images containing patch/performance/rhythm sound data.
- Compiled firmware images.
- SD-card images.
- External test dumps.
