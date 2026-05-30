# Features and limitations

This page summarizes the main features and current limitations of the MiniJV880-CardRAM line.

## Added features and improvements

Compared with the original Mini-JV880pi starting point, this line adds and improves:

- Raspberry Pi 4-focused bare-metal MiniJV880 build.
- SR-JV80 expansion ROM support through overlay/menu workflow.
- PN-JV80 ROM support through overlay/menu workflow with encoder long press.
- `PN-JV80/` local SysEx folder workflow through overlay/menu workflow with encoder long press.
- Optional Roland RD-500 patches workflow from the SR overlay/menu.
- Full SD-backed CardRAM support, including initialize, write, copy and related JV-880 workflows.
- `CARD-RAM/` collection workflow with `current.txt` active-card selection.
- Card-ready probe workaround for reliable Card/C navigation.
- HTTP CardRAM list/select/rename/delete pages.
- DATA short press for SR overlay access.
- DATA long press plus rotary encoder for native A/B/I/C bank selection.
- Usable 12-button panel, with erratic button behavior fixed.
- Bit-banged serial debug log output on GPIO4, leaving the standard UART pins GPIO14/GPIO15 free for MIDI.
- Embedded local HTTP server for status, SD-card browsing and maintenance workflows.
- Kernel and INI staging/maintenance plus reboot workflow over the local network.
- PC-side CardRAM command-line tool.
- PC-side Tkinter CardRAM manager GUI.
- PC-side external Remote GUI for development and maintenance testing.
- Remote GUI LCD/LED readback support, including `/rlcd.txt`, passive serial readback, cursor tracking and dot-matrix LCD rendering.
- Remote GUI display customization, including LCD color preset, background tone, text color, invert option and character render mode.
- Remote hold gesture support for interactive controls such as PREVIEW.
- TFTP support for larger local file transfers, including CardRAM image upload/download.
- Robust rotary encoder driver/decoder, solving click-loss issues.
- Improved LCD handling and ghost-character fixes.
- Kernel-only public package helper for creating minimal Raspberry Pi 4 kernel release ZIPs.
- Documented reproducible public build workflow using Arm GNU Toolchain 13.2.Rel1.
- Public-clean source structure, with private, test and Roland material excluded.

## Current limitations

- This is a practical MiniJV880 hardware/software adaptation, not a 1:1 recreation of the original Roland JV-880 front panel.
- The physical button layout is not complete: the COMPARE button is not implemented, and ENTER is performed by the encoder switch.
- Some workflows use MiniJV880-specific controls, especially DATA short/long press.
- Raspberry Pi 4 Model B is the main tested target; Raspberry Pi 5 and lower-memory/older boards are not validation targets for this release.
- SR-JV80 images are loaded into RAM at boot; each valid SR image uses about 8 MB.
- Ethernet is the recommended network path; Wi-Fi/WLAN is present only as experimental work and is not considered reliable or supported in this release.
- The embedded HTTP/TFTP maintenance interface is intended for trusted local networks only, not Internet exposure.
- The full Remote GUI is a PC-side tool: it is not compiled into the firmware and is not served by the embedded MiniJV880 HTTP server.
- The embedded HTTP server should remain small and focused on technical endpoints and maintenance actions.
- The public repository remains source-only: it does not include ready-made SD-card images, Raspberry Pi boot files, Roland ROM/NVRAM, SR images, SysEx banks, RD-500 files, personal configuration files or CardRAM sound data.
- Some GitHub releases may provide a separate kernel-only convenience package. That package contains only the compiled kernel plus checksum/readme/manifest files; it is not a complete SD-card image.
- Raspberry Pi 4 / AArch64 source builds should use Arm GNU Toolchain 13.2.Rel1. Arm GNU Toolchain 15.2.Rel1 is currently not recommended with the recorded Circle/newlib submodules.
- SD-card folder names and shallow folder-depth rules must be respected.
- The CardRAM tools are not a complete parameter-level JV-880 patch/performance editor; some areas, especially rhythm data, are handled as raw data.
- Experimental SysEx-to-CardRAM mapping research is not included in the public-clean repository.
