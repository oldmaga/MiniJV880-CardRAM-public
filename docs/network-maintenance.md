# Network maintenance

MiniJV880-CardRAM includes a small embedded local network maintenance layer.

It is intended for use on a trusted local network, mainly through wired Ethernet.

## Recommended network path

Ethernet is the recommended network path for this release.

Wi-Fi/WLAN code and configuration experiments may exist in the source tree, but Wi-Fi is not considered reliable or supported in this line.

For reliable maintenance operations, use wired Ethernet.

## Embedded HTTP maintenance server

MiniJV880 includes an embedded HTTP server for local maintenance pages.

The HTTP server is used for workflows such as:

- status pages;
- SD-card browsing;
- CardRAM status and management;
- CardRAM list/select/rename/delete;
- PN-JV80 folder browsing and file management;
- INI staging and review;
- kernel staging/activation workflow;
- explicit reboot workflow.

The HTTP server is not intended to be exposed to the Internet.

Use it only on a trusted local network.

## TFTP support

TFTP is used for larger local file transfers where HTTP is not suitable.

Typical TFTP workflows include:

- kernel upload/staging;
- INI upload/staging;
- CardRAM image upload/download;
- SysEx file upload/download workflows.

CardRAM TFTP transfers are restricted to:

    SD:/CARD-RAM/

CardRAM images must be exactly:

    32768 bytes

## Conservative public defaults

The public/default configuration is intentionally conservative.

Network write operations, TFTP and folder exposure should only be enabled after reviewing:

- your SD-card contents;
- your local network setup;
- whether the MiniJV880 is connected only to a trusted network.

## Wi-Fi note

Wi-Fi/WLAN is not considered reliable or supported in this release.

Do not commit personal Wi-Fi configuration files such as:

    wpa_supplicant.conf

or router/hotspot-specific variants.

If you experiment with Wi-Fi, keep those files local and out of the repository.

## Dualboot-aware kernel maintenance

Dualboot-aware builds expose a plain-text boot layout endpoint:

    /boot-layout.txt

This endpoint reports the managed MiniJV880 kernel paths currently used by the firmware.

Singleboot legacy layout:

    SD:/kernel8-rpi4.img
    SD:/kernel8-rpi4.img.new
    SD:/kernel8-rpi4.img.bak

Dualboot layout:

    SD:/minijv880/kernel8-rpi4.img
    SD:/minijv880/kernel8-rpi4.img.new
    SD:/minijv880/kernel8-rpi4.img.bak

The optional Raspberry Pi boot configuration example for this layout is:

    src/config-dualboot-data-gpio12.txt

Copy it to the SD-card root as `config.txt` only when you intentionally want to enable the MiniJV880/MiniDexed dualboot selector.

When a MiniDexed kernel is present at:

    SD:/minidexed/kernel8-rpi4.img

it is treated as read-only/detection-only by MiniJV880. MiniJV880 HTTP/TFTP kernel maintenance must not create, overwrite, activate, back up or delete the MiniDexed kernel.

The public TFTP remote name for MiniJV880 kernel staging remains:

    kernel8-rpi4.img

The firmware maps that remote name to the current managed MiniJV880 staged kernel path.
