# MiniJV880 ![Github Build Status](https://github.com/giulioz/mini-jv880/actions/workflows/build.yml/badge.svg)

![JV880](https://github.com/user-attachments/assets/04f92b10-9d01-4172-8356-1d199547d564)

Mini-JV880pi is a rompler-style synthesizer closely modeled on the famous JV-880 by a well-known Japanese manufacturer running on a bare metal Raspberry Pi (without a Linux kernel or operating system).

## This fork

This fork focuses on turning MiniJV880pi into a reliable standalone
hardware synthesizer running on Raspberry Pi 4 in bare-metal mode.

Main additions and improvements:

- Stable SR-JV80 expansion ROM hot-swap system
- SR overlay menu with safe audio engine pause/restart
- Full 12-button front-panel GPIO matrix support
- DATA button repurposed to open/close the SR menu
- Play Mode auto-recovery (Patch/Performance sync fix)
- Serial debug output via GPIO4 (bit-banged UART)
- Improved LCD handling and ghost-character fixes
- Robust error handling for missing/invalid ROMs

The goal of this fork is long-term hardware reliability and behaviour
closely matching a real JV-880 front panel.


## Acknowledgements

This project stands on the shoulders of giants. Special thanks to:

- [giulioz](https://github.com/giulioz) for the original idea of running Mini-JV880 (Nuked-SC55) on Raspberry Pi  
- [plamikcho](https://github.com/plamikcho) for early modifications of giulioz’s code  
- [Sterr1](https://github.com/Sterr1/Mini-JV880pi) for the Mini-JV880pi project and Raspberry Pi hardware integration work  
- [nukeykt](https://github.com/nukeykt) for the [Nuked SC-55](https://github.com/nukeykt/Nuked-SC55) emulator, on which this synth is based  
- [probonopd](https://github.com/probonopd) for [MiniDexed](https://github.com/probonopd/MiniDexed), which served as the basis for this bare-metal implementation  
- [rsta2](https://github.com/rsta2) for [Circle](https://github.com/rsta2/circle), the bare-metal Raspberry Pi framework used by this project  
- [smuehlst](https://github.com/smuehlst) for [circle-stdlib](https://github.com/smuehlst/circle-stdlib), providing Standard C/C++ library support
