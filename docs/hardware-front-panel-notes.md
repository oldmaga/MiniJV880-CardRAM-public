# Hardware and front-panel notes

MiniJV880-CardRAM is designed for practical use on MiniJV880-style hardware, not as a perfect one-to-one recreation of every JV-880 front-panel detail.

Some hardware and UI compromises are intentional.

## Target hardware

This release line is developed and tested mainly on Raspberry Pi 4 Model B.

Other Raspberry Pi boards may require code or configuration changes and are not the primary target of this documentation.

## Front-panel compatibility

The MiniJV880 front panel is not a complete physical clone of the original Roland JV-880 panel.

In particular:

- not every original JV-880 button is necessarily available as a dedicated physical button;
- some physical controls may be repurposed for MiniJV880-specific workflows;
- the priority is practical access to the most useful functions, not exact physical-panel emulation.

## DATA button behavior

The DATA control is used by MiniJV880-specific SR Extension Card handling.

To preserve that workflow while still allowing native JV-880 bank selection, this release line uses a practical split:

- DATA short press keeps the MiniJV880 SR Extension Card workflow;
- DATA long press is passed through to the JV-880 firmware in supported contexts.

This allows native A/B/I/C bank selection without sacrificing the existing SR workflow.

## Card/C selection contexts

DATA long press is used for Card/C selection in the supported JV-880 contexts where this has been implemented and tested, including:

- Patch Play;
- Performance Play;
- Patch Write;
- Performance Write;
- Patch Copy;
- Performance Copy.

This provides a consistent access method for Card/C operations.

## MONITOR behavior

Earlier development builds experimented with using MONITOR as a Card/C access helper in some Utility/Write/Copy contexts.

The current consolidated approach is DATA long press.

MONITOR is preserved for its own Performance-related behavior and should not be treated as the general Card/C selector in this release line.

## COMPARE and exact JV-880 mapping

A dedicated physical COMPARE button may not be present on MiniJV880 hardware.

This is one of the known differences between a practical MiniJV880 control surface and a full original JV-880 front panel.

## Design goal

The front-panel design goal is:

    practical MiniJV880 operation first,
    exact JV-880 physical-panel reproduction second.

When reading the original JV-880 manual, keep this distinction in mind.
