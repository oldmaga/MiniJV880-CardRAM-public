#!/usr/bin/env python3
import argparse
import os
import sys
from pathlib import Path


CARD_SIZE = 32768

# Provisional JV-880 CardRAM layout.
#
# This matches the observed raw CardRAM structure and the JV-880 documented
# Data Card content model:
#   - 16 Performances
#   - 64 Patches
#   - 1 Rhythm Set area
#
# Header:
#   0x0000 - 0x001F
#
# Performance area:
#   16 entries, 0x00CC bytes each, names at entry start.
#
# Patch area:
#   64 entries, 0x016A bytes each, names at entry start.
#
# Rhythm area:
#   starts after the 64 patch entries. Its internal structure is not decoded yet.
PERFORMANCE_AREA_START = 0x0020
PERFORMANCE_SLOT_SIZE = 0x00CC
PERFORMANCE_NAME_BYTES = 12
PERFORMANCE_SLOT_COUNT = 16

PATCH_AREA_START = 0x0CE0
PATCH_SLOT_SIZE = 0x016A
PATCH_NAME_BYTES = 12
PATCH_SLOT_COUNT = 64

RHYTHM_AREA_START = PATCH_AREA_START + PATCH_SLOT_COUNT * PATCH_SLOT_SIZE
RHYTHM_AREA_SIZE = CARD_SIZE - RHYTHM_AREA_START

FNV_OFFSET = 2166136261
FNV_PRIME = 16777619


def fnv1a32(data: bytes) -> int:
    digest = FNV_OFFSET

    for b in data:
        digest = ((digest ^ b) * FNV_PRIME) & 0xFFFFFFFF

    return digest


def read_file(path: Path) -> bytes:
    if not path.is_file():
        raise ValueError(f"not a regular file: {path}")

    return path.read_bytes()


def is_valid_card_size(data: bytes) -> bool:
    return len(data) == CARD_SIZE


def require_card(path: Path, data: bytes) -> None:
    if not is_valid_card_size(data):
        raise ValueError(
            f"{path}: invalid CardRAM size: {len(data)} bytes, expected {CARD_SIZE} bytes"
        )


def find_diff_ranges(left: bytes, right: bytes):
    ranges = []
    start = None

    for i, (a, b) in enumerate(zip(left, right)):
        if a != b:
            if start is None:
                start = i
        else:
            if start is not None:
                ranges.append((start, i - 1))
                start = None

    if start is not None:
        ranges.append((start, len(left) - 1))

    return ranges


def count_diff_bytes(left: bytes, right: bytes) -> int:
    return sum(1 for a, b in zip(left, right) if a != b)


def format_range(start: int, end: int) -> str:
    length = end - start + 1

    if start == end:
        return f"  0x{start:04X}          1 byte"

    return f"  0x{start:04X} - 0x{end:04X}   {length} bytes"


def hex_bytes(data: bytes, start: int, length: int) -> str:
    chunk = data[start:start + length]
    return " ".join(f"{b:02X}" for b in chunk)


def ascii_preview(data: bytes, start: int, length: int) -> str:
    chunk = data[start:start + length]

    chars = []
    for b in chunk:
        if 32 <= b <= 126:
            chars.append(chr(b))
        else:
            chars.append(".")

    return "".join(chars)


def decode_patch_name(raw: bytes) -> str:
    chars = []

    for b in raw:
        if 32 <= b <= 126:
            chars.append(chr(b))
        else:
            chars.append(".")

    return "".join(chars).rstrip()


def is_printable_ascii_byte(value: int) -> bool:
    return 32 <= value <= 126


def clean_ascii_run(raw: bytes) -> str:
    return "".join(chr(b) if is_printable_ascii_byte(b) else "." for b in raw).strip()


def encode_card_name(name: str, field_size: int) -> bytes:
    if name == "":
        raise ValueError("name cannot be empty")

    raw = name.encode("ascii", errors="strict")

    if len(raw) > field_size:
        raise ValueError(f"name is too long: {len(raw)} bytes, maximum is {field_size}")

    for value in raw:
        if not is_printable_ascii_byte(value):
            raise ValueError("name must contain printable ASCII characters only")

    return raw.ljust(field_size, b" ")


def write_or_print(text: str, report_path: str | None) -> None:
    if report_path:
        Path(report_path).write_text(text, encoding="utf-8")
    else:
        print(text, end="")


def validate_output_path(output_path: Path, input_paths: list[Path]) -> None:
    if output_path.exists():
        raise ValueError(f"output file already exists: {output_path}")

    output_parent = output_path.parent
    if str(output_parent) != "" and not output_parent.exists():
        raise ValueError(f"output directory does not exist: {output_parent}")

    output_resolved = output_path.resolve()

    for input_path in input_paths:
        if output_resolved == input_path.resolve():
            raise ValueError(f"output file must not overwrite input file: {output_path}")


def command_info(args) -> int:
    path = Path(args.file)

    try:
        data = read_file(path)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    digest = fnv1a32(data)
    valid = is_valid_card_size(data)

    lines = [
        "MiniJV880 CardRAM info",
        "======================",
        "",
        f"File:   {path.name}",
        f"Path:   {path.resolve()}",
        f"Size:   {len(data)} bytes",
        f"Valid:  {'yes' if valid else 'no'}",
        f"Digest: 0x{digest:08X}",
        "",
    ]

    if not valid:
        lines.extend([
            f"Expected CardRAM size: {CARD_SIZE} bytes",
            "",
        ])

    write_or_print("\n".join(lines), args.report)

    return 0 if valid else 1


def render_compare_report(left_path: Path, right_path: Path, left: bytes, right: bytes, max_ranges: int) -> tuple[str, bool]:
    diff_bytes = count_diff_bytes(left, right)
    ranges = find_diff_ranges(left, right)
    equal = diff_bytes == 0

    left_digest = fnv1a32(left)
    right_digest = fnv1a32(right)

    lines = [
        "MiniJV880 CardRAM compare",
        "=========================",
        "",
        f"Left:   {left_path.name}",
        f"Path:   {left_path.resolve()}",
        f"Size:   {len(left)} bytes",
        f"Digest: 0x{left_digest:08X}",
        "",
        f"Right:  {right_path.name}",
        f"Path:   {right_path.resolve()}",
        f"Size:   {len(right)} bytes",
        f"Digest: 0x{right_digest:08X}",
        "",
        f"Equal:             {'yes' if equal else 'no'}",
        f"Different bytes:   {diff_bytes}",
        f"Different ranges:  {len(ranges)}",
        "",
    ]

    if ranges:
        lines.append("Ranges:")

        shown = ranges if max_ranges == 0 else ranges[:max_ranges]

        for start, end in shown:
            lines.append(format_range(start, end))

        if max_ranges != 0 and len(ranges) > max_ranges:
            lines.append("")
            lines.append(f"... truncated after {max_ranges} ranges")

        lines.append("")

    return "\n".join(lines), equal


def command_compare(args) -> int:
    left_path = Path(args.left)
    right_path = Path(args.right)

    try:
        left = read_file(left_path)
        right = read_file(right_path)
        require_card(left_path, left)
        require_card(right_path, right)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    text, equal = render_compare_report(
        left_path,
        right_path,
        left,
        right,
        args.max_ranges,
    )

    write_or_print(text, args.report)

    return 0 if equal else 1


def render_diff_report(left_path: Path, right_path: Path, left: bytes, right: bytes, max_ranges: int, preview_bytes: int) -> tuple[str, bool]:
    compare_text, equal = render_compare_report(
        left_path,
        right_path,
        left,
        right,
        max_ranges,
    )

    ranges = find_diff_ranges(left, right)

    lines = [
        compare_text.rstrip(),
        "",
        "Diff preview",
        "------------",
        "",
    ]

    if not ranges:
        lines.append("No differences.")
        lines.append("")
        return "\n".join(lines), True

    shown = ranges if max_ranges == 0 else ranges[:max_ranges]

    for index, (start, end) in enumerate(shown, start=1):
        length = min(preview_bytes, end - start + 1)

        lines.append(f"Range {index}: 0x{start:04X} - 0x{end:04X}")
        lines.append(f"Left hex:   {hex_bytes(left, start, length)}")
        lines.append(f"Right hex:  {hex_bytes(right, start, length)}")
        lines.append(f"Left text:  {ascii_preview(left, start, length)}")
        lines.append(f"Right text: {ascii_preview(right, start, length)}")

        if (end - start + 1) > preview_bytes:
            lines.append(f"... preview truncated to {preview_bytes} bytes")

        lines.append("")

    if max_ranges != 0 and len(ranges) > max_ranges:
        lines.append(f"Diff preview truncated after {max_ranges} ranges.")
        lines.append("")

    return "\n".join(lines), False


def command_diff(args) -> int:
    left_path = Path(args.left)
    right_path = Path(args.right)

    try:
        left = read_file(left_path)
        right = read_file(right_path)
        require_card(left_path, left)
        require_card(right_path, right)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    text, equal = render_diff_report(
        left_path,
        right_path,
        left,
        right,
        args.max_ranges,
        args.preview_bytes,
    )

    write_or_print(text, args.report)

    return 0 if equal else 1


def render_scan_names_report(
    path: Path,
    data: bytes,
    min_length: int,
    max_results: int,
) -> str:
    lines = [
        "MiniJV880 CardRAM ASCII/name scan",
        "=================================",
        "",
        f"File:   {path.name}",
        f"Path:   {path.resolve()}",
        f"Size:   {len(data)} bytes",
        f"Digest: 0x{fnv1a32(data):08X}",
        "",
        "Scan parameters:",
        f"  minimum run length: {min_length}",
        f"  maximum results:    {'all' if max_results == 0 else max_results}",
        "",
        "Runs:",
        "  #    Offset   Length  Text",
        "  ---  -------  ------  ----",
    ]

    results = []
    start = None

    for index, value in enumerate(data):
        if is_printable_ascii_byte(value):
            if start is None:
                start = index
        else:
            if start is not None:
                length = index - start
                if length >= min_length:
                    results.append((start, length, clean_ascii_run(data[start:index])))
                start = None

    if start is not None:
        length = len(data) - start
        if length >= min_length:
            results.append((start, length, clean_ascii_run(data[start:])))

    shown = results if max_results == 0 else results[:max_results]

    for result_index, (offset, length, text) in enumerate(shown, start=1):
        lines.append(
            f"  {result_index:03d}  0x{offset:04X}   {length:6d}  {text}"
        )

    if not results:
        lines.append("  No printable ASCII runs found.")

    if max_results != 0 and len(results) > max_results:
        lines.extend([
            "",
            f"... truncated after {max_results} results",
        ])

    lines.extend([
        "",
        f"Total runs found: {len(results)}",
        "",
    ])

    return "\n".join(lines)


def command_scan_names(args) -> int:
    path = Path(args.file)

    try:
        data = read_file(path)
        require_card(path, data)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    text = render_scan_names_report(
        path,
        data,
        args.min_length,
        args.max_results,
    )

    write_or_print(text, args.report)

    return 0


def render_probe_stride_report(
    path: Path,
    data: bytes,
    start: int,
    stride: int,
    count: int,
    name_bytes: int,
) -> str:
    lines = [
        "MiniJV880 CardRAM stride probe",
        "==============================",
        "",
        f"File:   {path.name}",
        f"Path:   {path.resolve()}",
        f"Size:   {len(data)} bytes",
        f"Digest: 0x{fnv1a32(data):08X}",
        "",
        "Probe parameters:",
        f"  start:      0x{start:04X}",
        f"  stride:     0x{stride:04X} ({stride} bytes)",
        f"  count:      {count}",
        f"  name bytes: {name_bytes}",
        "",
        "Slots:",
        "  #    Offset   Name          Block digest",
        "  ---  -------  ------------  ------------",
    ]

    found = 0

    for index in range(count):
        offset = start + index * stride

        if offset >= len(data):
            break

        name_end = min(offset + name_bytes, len(data))
        block_end = min(offset + stride, len(data))

        raw_name = data[offset:name_end]
        block_data = data[offset:block_end]

        name = decode_patch_name(raw_name)
        if name == "":
            name = "<blank>"

        lines.append(
            f"  {index + 1:03d}  0x{offset:04X}   {name:<12.12}  0x{fnv1a32(block_data):08X}"
        )

        found += 1

    lines.extend([
        "",
        f"Entries shown: {found}",
        "",
    ])

    return "\n".join(lines)


def command_probe_stride(args) -> int:
    path = Path(args.file)

    try:
        data = read_file(path)
        require_card(path, data)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    text = render_probe_stride_report(
        path,
        data,
        args.start,
        args.stride,
        args.count,
        args.name_bytes,
    )

    write_or_print(text, args.report)

    return 0


def render_compare_performances_report(
    left_path: Path,
    right_path: Path,
    left: bytes,
    right: bytes,
    slot_count: int,
) -> tuple[str, bool]:
    lines = [
        "MiniJV880 CardRAM performance compare",
        "=====================================",
        "",
        f"Left:   {left_path.name}",
        f"Path:   {left_path.resolve()}",
        f"Digest: 0x{fnv1a32(left):08X}",
        "",
        f"Right:  {right_path.name}",
        f"Path:   {right_path.resolve()}",
        f"Digest: 0x{fnv1a32(right):08X}",
        "",
        "Layout:",
        f"  performance area start: 0x{PERFORMANCE_AREA_START:04X}",
        f"  performance slot size:  0x{PERFORMANCE_SLOT_SIZE:04X} ({PERFORMANCE_SLOT_SIZE} bytes)",
        f"  performance name bytes: {PERFORMANCE_NAME_BYTES}",
        f"  compared slots:         {slot_count}",
        "",
        "Slots:",
        "  #    Offset   Left name     Right name    Equal  Left digest  Right digest",
        "  ---  -------  ------------  ------------  -----  -----------  ------------",
    ]

    all_equal = True

    for slot in range(slot_count):
        offset = PERFORMANCE_AREA_START + slot * PERFORMANCE_SLOT_SIZE

        if offset + PERFORMANCE_SLOT_SIZE > len(left) or offset + PERFORMANCE_SLOT_SIZE > len(right):
            break

        left_slot = left[offset:offset + PERFORMANCE_SLOT_SIZE]
        right_slot = right[offset:offset + PERFORMANCE_SLOT_SIZE]

        left_name = decode_patch_name(left_slot[:PERFORMANCE_NAME_BYTES])
        right_name = decode_patch_name(right_slot[:PERFORMANCE_NAME_BYTES])

        if left_name == "":
            left_name = "<blank>"

        if right_name == "":
            right_name = "<blank>"

        equal = left_slot == right_slot
        if not equal:
            all_equal = False

        lines.append(
            f"  {slot + 1:03d}  0x{offset:04X}   "
            f"{left_name:<12.12}  "
            f"{right_name:<12.12}  "
            f"{'yes' if equal else 'no ':<5}  "
            f"0x{fnv1a32(left_slot):08X}   "
            f"0x{fnv1a32(right_slot):08X}"
        )

    lines.extend([
        "",
        f"Performance slots equal: {'yes' if all_equal else 'no'}",
        "",
    ])

    return "\n".join(lines), all_equal


def command_compare_performances(args) -> int:
    left_path = Path(args.left)
    right_path = Path(args.right)

    try:
        left = read_file(left_path)
        right = read_file(right_path)
        require_card(left_path, left)
        require_card(right_path, right)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    text, equal = render_compare_performances_report(
        left_path,
        right_path,
        left,
        right,
        args.slots,
    )

    write_or_print(text, args.report)

    return 0 if equal else 1


def render_compare_patches_report(
    left_path: Path,
    right_path: Path,
    left: bytes,
    right: bytes,
    slot_count: int,
) -> tuple[str, bool]:
    lines = [
        "MiniJV880 CardRAM patch compare",
        "===============================",
        "",
        f"Left:   {left_path.name}",
        f"Path:   {left_path.resolve()}",
        f"Digest: 0x{fnv1a32(left):08X}",
        "",
        f"Right:  {right_path.name}",
        f"Path:   {right_path.resolve()}",
        f"Digest: 0x{fnv1a32(right):08X}",
        "",
        "Layout hypothesis:",
        f"  patch area start: 0x{PATCH_AREA_START:04X}",
        f"  patch slot size:  0x{PATCH_SLOT_SIZE:04X} ({PATCH_SLOT_SIZE} bytes)",
        f"  patch name bytes: {PATCH_NAME_BYTES}",
        f"  compared slots:   {slot_count}",
        "",
        "Slots:",
        "  #    Offset   Left name     Right name    Equal  Left digest  Right digest",
        "  ---  -------  ------------  ------------  -----  -----------  ------------",
    ]

    all_equal = True

    for slot in range(slot_count):
        offset = PATCH_AREA_START + slot * PATCH_SLOT_SIZE

        if offset + PATCH_SLOT_SIZE > len(left) or offset + PATCH_SLOT_SIZE > len(right):
            break

        left_slot = left[offset:offset + PATCH_SLOT_SIZE]
        right_slot = right[offset:offset + PATCH_SLOT_SIZE]

        left_name = decode_patch_name(left_slot[:PATCH_NAME_BYTES])
        right_name = decode_patch_name(right_slot[:PATCH_NAME_BYTES])

        if left_name == "":
            left_name = "<blank>"

        if right_name == "":
            right_name = "<blank>"

        equal = left_slot == right_slot
        if not equal:
            all_equal = False

        lines.append(
            f"  {slot + 1:03d}  0x{offset:04X}   "
            f"{left_name:<12.12}  "
            f"{right_name:<12.12}  "
            f"{'yes' if equal else 'no ':<5}  "
            f"0x{fnv1a32(left_slot):08X}   "
            f"0x{fnv1a32(right_slot):08X}"
        )

    lines.extend([
        "",
        f"Patch slots equal: {'yes' if all_equal else 'no'}",
        "",
    ])

    return "\n".join(lines), all_equal


def command_compare_patches(args) -> int:
    left_path = Path(args.left)
    right_path = Path(args.right)

    try:
        left = read_file(left_path)
        right = read_file(right_path)
        require_card(left_path, left)
        require_card(right_path, right)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    text, equal = render_compare_patches_report(
        left_path,
        right_path,
        left,
        right,
        args.slots,
    )

    write_or_print(text, args.report)

    return 0 if equal else 1


def render_slot_list_report(
    title: str,
    path: Path,
    data: bytes,
    area_start: int,
    slot_size: int,
    name_bytes: int,
    slot_count: int,
    label: str,
) -> str:
    underline = "=" * len(title)

    lines = [
        title,
        underline,
        "",
        f"File:   {path.name}",
        f"Path:   {path.resolve()}",
        f"Size:   {len(data)} bytes",
        f"Digest: 0x{fnv1a32(data):08X}",
        "",
        "Layout:",
        f"  {label} area start: 0x{area_start:04X}",
        f"  {label} slot size:  0x{slot_size:04X} ({slot_size} bytes)",
        f"  {label} name bytes: {name_bytes}",
        f"  listed slots:       {slot_count}",
        "",
        "Slots:",
        "  #    Offset   Name          Slot digest",
        "  ---  -------  ------------  -----------",
    ]

    slot = 0
    offset = area_start

    while slot < slot_count and offset + slot_size <= len(data):
        slot_data = data[offset:offset + slot_size]
        raw_name = slot_data[:name_bytes]
        name = decode_patch_name(raw_name)

        if name == "":
            name = "<blank>"

        lines.append(
            f"  {slot + 1:03d}  0x{offset:04X}   {name:<12.12}  0x{fnv1a32(slot_data):08X}"
        )

        slot += 1
        offset += slot_size

    lines.extend([
        "",
        f"Total complete slots found: {slot}",
        f"End offset after last slot: 0x{offset:04X}",
        "",
    ])

    return "\n".join(lines)


def render_list_performances_report(path: Path, data: bytes, slot_count: int) -> str:
    return render_slot_list_report(
        "MiniJV880 CardRAM performance list",
        path,
        data,
        PERFORMANCE_AREA_START,
        PERFORMANCE_SLOT_SIZE,
        PERFORMANCE_NAME_BYTES,
        slot_count,
        "performance",
    )


def render_list_patches_report(path: Path, data: bytes, slot_count: int) -> str:
    return render_slot_list_report(
        "MiniJV880 CardRAM patch list",
        path,
        data,
        PATCH_AREA_START,
        PATCH_SLOT_SIZE,
        PATCH_NAME_BYTES,
        slot_count,
        "patch",
    )


def swap_performance_slots(
    source: bytes,
    slot_a: int,
    slot_b: int,
) -> tuple[bytes, int, int, bytes, bytes]:
    if slot_a < 1 or slot_a > PERFORMANCE_SLOT_COUNT:
        raise ValueError(f"performance slot A out of range: {slot_a}")

    if slot_b < 1 or slot_b > PERFORMANCE_SLOT_COUNT:
        raise ValueError(f"performance slot B out of range: {slot_b}")

    if slot_a == slot_b:
        raise ValueError("performance slots must be different")

    offset_a = PERFORMANCE_AREA_START + (slot_a - 1) * PERFORMANCE_SLOT_SIZE
    offset_b = PERFORMANCE_AREA_START + (slot_b - 1) * PERFORMANCE_SLOT_SIZE

    slot_a_data = source[offset_a:offset_a + PERFORMANCE_SLOT_SIZE]
    slot_b_data = source[offset_b:offset_b + PERFORMANCE_SLOT_SIZE]

    if len(slot_a_data) != PERFORMANCE_SLOT_SIZE:
        raise ValueError("performance slot A is incomplete")

    if len(slot_b_data) != PERFORMANCE_SLOT_SIZE:
        raise ValueError("performance slot B is incomplete")

    output = bytearray(source)
    output[offset_a:offset_a + PERFORMANCE_SLOT_SIZE] = slot_b_data
    output[offset_b:offset_b + PERFORMANCE_SLOT_SIZE] = slot_a_data

    return bytes(output), offset_a, offset_b, slot_a_data, slot_b_data


def command_swap_performances(args) -> int:
    source_path = Path(args.source)
    output_path = Path(args.output)

    try:
        source = read_file(source_path)
        require_card(source_path, source)
        validate_output_path(output_path, [source_path])

        output, offset_a, offset_b, old_slot_a_data, old_slot_b_data = swap_performance_slots(
            source,
            args.slot_a,
            args.slot_b,
        )

        output_path.write_bytes(output)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    old_name_a = decode_patch_name(old_slot_a_data[:PERFORMANCE_NAME_BYTES])
    old_name_b = decode_patch_name(old_slot_b_data[:PERFORMANCE_NAME_BYTES])

    new_slot_a_data = output[offset_a:offset_a + PERFORMANCE_SLOT_SIZE]
    new_slot_b_data = output[offset_b:offset_b + PERFORMANCE_SLOT_SIZE]

    new_name_a = decode_patch_name(new_slot_a_data[:PERFORMANCE_NAME_BYTES])
    new_name_b = decode_patch_name(new_slot_b_data[:PERFORMANCE_NAME_BYTES])

    if old_name_a == "":
        old_name_a = "<blank>"

    if old_name_b == "":
        old_name_b = "<blank>"

    if new_name_a == "":
        new_name_a = "<blank>"

    if new_name_b == "":
        new_name_b = "<blank>"

    lines = [
        "MiniJV880 CardRAM swap performances completed",
        "============================================",
        "",
        f"Source file: {source_path}",
        f"Output file: {output_path}",
        "",
        "Performance swap:",
        f"  slot A:       {args.slot_a}",
        f"  offset A:     0x{offset_a:04X}",
        f"  old name A:   {old_name_a}",
        f"  new name A:   {new_name_a}",
        f"  old digest A: 0x{fnv1a32(old_slot_a_data):08X}",
        f"  new digest A: 0x{fnv1a32(new_slot_a_data):08X}",
        "",
        f"  slot B:       {args.slot_b}",
        f"  offset B:     0x{offset_b:04X}",
        f"  old name B:   {old_name_b}",
        f"  new name B:   {new_name_b}",
        f"  old digest B: 0x{fnv1a32(old_slot_b_data):08X}",
        f"  new digest B: 0x{fnv1a32(new_slot_b_data):08X}",
        "",
        f"Source digest: 0x{fnv1a32(source):08X}",
        f"Output digest: 0x{fnv1a32(output):08X}",
        "",
        "Original file was not modified.",
        "",
    ]

    text = "\n".join(lines)
    write_or_print(text, args.report)

    return 0


def swap_patch_slots(
    source: bytes,
    slot_a: int,
    slot_b: int,
) -> tuple[bytes, int, int, bytes, bytes]:
    if slot_a < 1 or slot_a > PATCH_SLOT_COUNT:
        raise ValueError(f"patch slot A out of range: {slot_a}")

    if slot_b < 1 or slot_b > PATCH_SLOT_COUNT:
        raise ValueError(f"patch slot B out of range: {slot_b}")

    if slot_a == slot_b:
        raise ValueError("patch slots must be different")

    offset_a = PATCH_AREA_START + (slot_a - 1) * PATCH_SLOT_SIZE
    offset_b = PATCH_AREA_START + (slot_b - 1) * PATCH_SLOT_SIZE

    slot_a_data = source[offset_a:offset_a + PATCH_SLOT_SIZE]
    slot_b_data = source[offset_b:offset_b + PATCH_SLOT_SIZE]

    if len(slot_a_data) != PATCH_SLOT_SIZE:
        raise ValueError("patch slot A is incomplete")

    if len(slot_b_data) != PATCH_SLOT_SIZE:
        raise ValueError("patch slot B is incomplete")

    output = bytearray(source)
    output[offset_a:offset_a + PATCH_SLOT_SIZE] = slot_b_data
    output[offset_b:offset_b + PATCH_SLOT_SIZE] = slot_a_data

    return bytes(output), offset_a, offset_b, slot_a_data, slot_b_data


def command_swap_patches(args) -> int:
    source_path = Path(args.source)
    output_path = Path(args.output)

    try:
        source = read_file(source_path)
        require_card(source_path, source)
        validate_output_path(output_path, [source_path])

        output, offset_a, offset_b, old_slot_a_data, old_slot_b_data = swap_patch_slots(
            source,
            args.slot_a,
            args.slot_b,
        )

        output_path.write_bytes(output)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    old_name_a = decode_patch_name(old_slot_a_data[:PATCH_NAME_BYTES])
    old_name_b = decode_patch_name(old_slot_b_data[:PATCH_NAME_BYTES])

    new_slot_a_data = output[offset_a:offset_a + PATCH_SLOT_SIZE]
    new_slot_b_data = output[offset_b:offset_b + PATCH_SLOT_SIZE]

    new_name_a = decode_patch_name(new_slot_a_data[:PATCH_NAME_BYTES])
    new_name_b = decode_patch_name(new_slot_b_data[:PATCH_NAME_BYTES])

    if old_name_a == "":
        old_name_a = "<blank>"

    if old_name_b == "":
        old_name_b = "<blank>"

    if new_name_a == "":
        new_name_a = "<blank>"

    if new_name_b == "":
        new_name_b = "<blank>"

    lines = [
        "MiniJV880 CardRAM swap patches completed",
        "========================================",
        "",
        f"Source file: {source_path}",
        f"Output file: {output_path}",
        "",
        "Patch swap:",
        f"  slot A:      {args.slot_a}",
        f"  offset A:    0x{offset_a:04X}",
        f"  old name A:  {old_name_a}",
        f"  new name A:  {new_name_a}",
        f"  old digest A: 0x{fnv1a32(old_slot_a_data):08X}",
        f"  new digest A: 0x{fnv1a32(new_slot_a_data):08X}",
        "",
        f"  slot B:      {args.slot_b}",
        f"  offset B:    0x{offset_b:04X}",
        f"  old name B:  {old_name_b}",
        f"  new name B:  {new_name_b}",
        f"  old digest B: 0x{fnv1a32(old_slot_b_data):08X}",
        f"  new digest B: 0x{fnv1a32(new_slot_b_data):08X}",
        "",
        f"Source digest: 0x{fnv1a32(source):08X}",
        f"Output digest: 0x{fnv1a32(output):08X}",
        "",
        "Original file was not modified.",
        "",
    ]

    text = "\n".join(lines)
    write_or_print(text, args.report)

    return 0


def set_performance_name(
    source: bytes,
    slot: int,
    name: str,
) -> tuple[bytes, int, bytes, bytes]:
    if slot < 1 or slot > PERFORMANCE_SLOT_COUNT:
        raise ValueError(f"performance slot out of range: {slot}")

    offset = PERFORMANCE_AREA_START + (slot - 1) * PERFORMANCE_SLOT_SIZE

    old_name_data = source[offset:offset + PERFORMANCE_NAME_BYTES]
    new_name_data = encode_card_name(name, PERFORMANCE_NAME_BYTES)

    if len(old_name_data) != PERFORMANCE_NAME_BYTES:
        raise ValueError("performance name field is incomplete")

    output = bytearray(source)
    output[offset:offset + PERFORMANCE_NAME_BYTES] = new_name_data

    return bytes(output), offset, old_name_data, new_name_data


def command_set_performance_name(args) -> int:
    source_path = Path(args.source)
    output_path = Path(args.output)

    try:
        source = read_file(source_path)
        require_card(source_path, source)
        validate_output_path(output_path, [source_path])

        output, offset, old_name_data, new_name_data = set_performance_name(
            source,
            args.slot,
            args.name,
        )

        output_path.write_bytes(output)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    old_name = decode_patch_name(old_name_data)
    new_name = decode_patch_name(new_name_data)

    if old_name == "":
        old_name = "<blank>"

    if new_name == "":
        new_name = "<blank>"

    old_slot_data = source[offset:offset + PERFORMANCE_SLOT_SIZE]
    new_slot_data = output[offset:offset + PERFORMANCE_SLOT_SIZE]

    lines = [
        "MiniJV880 CardRAM set performance name completed",
        "================================================",
        "",
        f"Source file: {source_path}",
        f"Output file: {output_path}",
        "",
        "Performance name edit:",
        f"  slot:        {args.slot}",
        f"  name offset: 0x{offset:04X}",
        f"  old name:    {old_name}",
        f"  new name:    {new_name}",
        "",
        f"  old slot digest: 0x{fnv1a32(old_slot_data):08X}",
        f"  new slot digest: 0x{fnv1a32(new_slot_data):08X}",
        "",
        f"Source digest: 0x{fnv1a32(source):08X}",
        f"Output digest: 0x{fnv1a32(output):08X}",
        "",
        "Original file was not modified.",
        "",
    ]

    text = "\n".join(lines)
    write_or_print(text, args.report)

    return 0


def set_patch_name(
    source: bytes,
    slot: int,
    name: str,
) -> tuple[bytes, int, bytes, bytes]:
    if slot < 1 or slot > PATCH_SLOT_COUNT:
        raise ValueError(f"patch slot out of range: {slot}")

    offset = PATCH_AREA_START + (slot - 1) * PATCH_SLOT_SIZE

    old_name_data = source[offset:offset + PATCH_NAME_BYTES]
    new_name_data = encode_card_name(name, PATCH_NAME_BYTES)

    if len(old_name_data) != PATCH_NAME_BYTES:
        raise ValueError("patch name field is incomplete")

    output = bytearray(source)
    output[offset:offset + PATCH_NAME_BYTES] = new_name_data

    return bytes(output), offset, old_name_data, new_name_data


def command_set_patch_name(args) -> int:
    source_path = Path(args.source)
    output_path = Path(args.output)

    try:
        source = read_file(source_path)
        require_card(source_path, source)
        validate_output_path(output_path, [source_path])

        output, offset, old_name_data, new_name_data = set_patch_name(
            source,
            args.slot,
            args.name,
        )

        output_path.write_bytes(output)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    old_name = decode_patch_name(old_name_data)
    new_name = decode_patch_name(new_name_data)

    if old_name == "":
        old_name = "<blank>"

    if new_name == "":
        new_name = "<blank>"

    old_slot_data = source[offset:offset + PATCH_SLOT_SIZE]
    new_slot_data = output[offset:offset + PATCH_SLOT_SIZE]

    lines = [
        "MiniJV880 CardRAM set patch name completed",
        "==========================================",
        "",
        f"Source file: {source_path}",
        f"Output file: {output_path}",
        "",
        "Patch name edit:",
        f"  slot:        {args.slot}",
        f"  name offset: 0x{offset:04X}",
        f"  old name:    {old_name}",
        f"  new name:    {new_name}",
        "",
        f"  old slot digest: 0x{fnv1a32(old_slot_data):08X}",
        f"  new slot digest: 0x{fnv1a32(new_slot_data):08X}",
        "",
        f"Source digest: 0x{fnv1a32(source):08X}",
        f"Output digest: 0x{fnv1a32(output):08X}",
        "",
        "Original file was not modified.",
        "",
    ]

    text = "\n".join(lines)
    write_or_print(text, args.report)

    return 0

INITIAL_SLOT_NAME = "INITIAL DATA"


def get_slot_offset(
    area_start: int,
    slot_size: int,
    slot_count: int,
    slot: int,
    area_label: str,
) -> int:
    if slot < 1 or slot > slot_count:
        raise ValueError(f"{area_label} slot out of range: {slot}")

    return area_start + (slot - 1) * slot_size


def read_complete_slot(
    data: bytes,
    area_start: int,
    slot_size: int,
    slot_count: int,
    slot: int,
    area_label: str,
) -> tuple[int, bytes]:
    offset = get_slot_offset(
        area_start,
        slot_size,
        slot_count,
        slot,
        area_label,
    )

    slot_data = data[offset:offset + slot_size]

    if len(slot_data) != slot_size:
        raise ValueError(f"{area_label} slot is incomplete: {slot}")

    return offset, slot_data


def slot_display_name(slot_data: bytes, name_bytes: int) -> str:
    name = decode_patch_name(slot_data[:name_bytes])

    if name == "":
        return "<blank>"

    return name


def find_initial_slot_template(
    data: bytes,
    area_start: int,
    slot_size: int,
    name_bytes: int,
    slot_count: int,
    area_label: str,
) -> tuple[int, int, bytes]:
    for slot in range(1, slot_count + 1):
        offset, slot_data = read_complete_slot(
            data,
            area_start,
            slot_size,
            slot_count,
            slot,
            area_label,
        )

        name = decode_patch_name(slot_data[:name_bytes])

        if name == INITIAL_SLOT_NAME:
            return slot, offset, slot_data

    raise ValueError(f"{area_label} INITIAL DATA slot template not found")


def is_initial_performance_slot(source: bytes, slot: int) -> bool:
    _target_offset, target_slot_data = read_complete_slot(
        source,
        PERFORMANCE_AREA_START,
        PERFORMANCE_SLOT_SIZE,
        PERFORMANCE_SLOT_COUNT,
        slot,
        "performance",
    )

    _template_slot, _template_offset, template_slot_data = find_initial_slot_template(
        source,
        PERFORMANCE_AREA_START,
        PERFORMANCE_SLOT_SIZE,
        PERFORMANCE_NAME_BYTES,
        PERFORMANCE_SLOT_COUNT,
        "performance",
    )

    return target_slot_data == template_slot_data


def is_initial_patch_slot(source: bytes, slot: int) -> bool:
    _target_offset, target_slot_data = read_complete_slot(
        source,
        PATCH_AREA_START,
        PATCH_SLOT_SIZE,
        PATCH_SLOT_COUNT,
        slot,
        "patch",
    )

    _template_slot, _template_offset, template_slot_data = find_initial_slot_template(
        source,
        PATCH_AREA_START,
        PATCH_SLOT_SIZE,
        PATCH_NAME_BYTES,
        PATCH_SLOT_COUNT,
        "patch",
    )

    return target_slot_data == template_slot_data


def clear_performance_slot(source: bytes, slot: int) -> tuple[bytes, int, bytes, int, int, bytes]:
    target_offset, old_target_slot_data = read_complete_slot(
        source,
        PERFORMANCE_AREA_START,
        PERFORMANCE_SLOT_SIZE,
        PERFORMANCE_SLOT_COUNT,
        slot,
        "performance",
    )

    template_slot, template_offset, template_slot_data = find_initial_slot_template(
        source,
        PERFORMANCE_AREA_START,
        PERFORMANCE_SLOT_SIZE,
        PERFORMANCE_NAME_BYTES,
        PERFORMANCE_SLOT_COUNT,
        "performance",
    )

    output = bytearray(source)
    output[target_offset:target_offset + PERFORMANCE_SLOT_SIZE] = template_slot_data

    return (
        bytes(output),
        target_offset,
        old_target_slot_data,
        template_slot,
        template_offset,
        template_slot_data,
    )


def clear_patch_slot(source: bytes, slot: int) -> tuple[bytes, int, bytes, int, int, bytes]:
    target_offset, old_target_slot_data = read_complete_slot(
        source,
        PATCH_AREA_START,
        PATCH_SLOT_SIZE,
        PATCH_SLOT_COUNT,
        slot,
        "patch",
    )

    template_slot, template_offset, template_slot_data = find_initial_slot_template(
        source,
        PATCH_AREA_START,
        PATCH_SLOT_SIZE,
        PATCH_NAME_BYTES,
        PATCH_SLOT_COUNT,
        "patch",
    )

    output = bytearray(source)
    output[target_offset:target_offset + PATCH_SLOT_SIZE] = template_slot_data

    return (
        bytes(output),
        target_offset,
        old_target_slot_data,
        template_slot,
        template_offset,
        template_slot_data,
    )


def command_clear_performance(args) -> int:
    source_path = Path(args.source)
    output_path = Path(args.output)

    try:
        source = read_file(source_path)
        require_card(source_path, source)
        validate_output_path(output_path, [source_path])

        (
            output,
            target_offset,
            old_target_slot_data,
            template_slot,
            template_offset,
            template_slot_data,
        ) = clear_performance_slot(
            source,
            args.slot,
        )

        output_path.write_bytes(output)

    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    new_target_slot_data = output[
        target_offset:
        target_offset + PERFORMANCE_SLOT_SIZE
    ]

    old_name = slot_display_name(old_target_slot_data, PERFORMANCE_NAME_BYTES)
    new_name = slot_display_name(new_target_slot_data, PERFORMANCE_NAME_BYTES)
    template_name = slot_display_name(template_slot_data, PERFORMANCE_NAME_BYTES)

    lines = [
        "MiniJV880 CardRAM clear performance completed",
        "============================================",
        "",
        f"Source file: {source_path}",
        f"Output file: {output_path}",
        "",
        "Performance clear:",
        f"  target slot:       {args.slot}",
        f"  target offset:     0x{target_offset:04X}",
        f"  old name:          {old_name}",
        f"  new name:          {new_name}",
        f"  old digest:        0x{fnv1a32(old_target_slot_data):08X}",
        f"  new digest:        0x{fnv1a32(new_target_slot_data):08X}",
        "",
        "Initial template:",
        f"  template slot:     {template_slot}",
        f"  template offset:   0x{template_offset:04X}",
        f"  template name:     {template_name}",
        f"  template digest:   0x{fnv1a32(template_slot_data):08X}",
        "",
        f"Source digest: 0x{fnv1a32(source):08X}",
        f"Output digest: 0x{fnv1a32(output):08X}",
        "",
        "Original file was not modified.",
        "",
    ]

    text = "\n".join(lines)
    write_or_print(text, args.report)

    return 0


def command_clear_patch(args) -> int:
    source_path = Path(args.source)
    output_path = Path(args.output)

    try:
        source = read_file(source_path)
        require_card(source_path, source)
        validate_output_path(output_path, [source_path])

        (
            output,
            target_offset,
            old_target_slot_data,
            template_slot,
            template_offset,
            template_slot_data,
        ) = clear_patch_slot(
            source,
            args.slot,
        )

        output_path.write_bytes(output)

    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    new_target_slot_data = output[
        target_offset:
        target_offset + PATCH_SLOT_SIZE
    ]

    old_name = slot_display_name(old_target_slot_data, PATCH_NAME_BYTES)
    new_name = slot_display_name(new_target_slot_data, PATCH_NAME_BYTES)
    template_name = slot_display_name(template_slot_data, PATCH_NAME_BYTES)

    lines = [
        "MiniJV880 CardRAM clear patch completed",
        "=======================================",
        "",
        f"Source file: {source_path}",
        f"Output file: {output_path}",
        "",
        "Patch clear:",
        f"  target slot:       {args.slot}",
        f"  target offset:     0x{target_offset:04X}",
        f"  old name:          {old_name}",
        f"  new name:          {new_name}",
        f"  old digest:        0x{fnv1a32(old_target_slot_data):08X}",
        f"  new digest:        0x{fnv1a32(new_target_slot_data):08X}",
        "",
        "Initial template:",
        f"  template slot:     {template_slot}",
        f"  template offset:   0x{template_offset:04X}",
        f"  template name:     {template_name}",
        f"  template digest:   0x{fnv1a32(template_slot_data):08X}",
        "",
        f"Source digest: 0x{fnv1a32(source):08X}",
        f"Output digest: 0x{fnv1a32(output):08X}",
        "",
        "Original file was not modified.",
        "",
    ]

    text = "\n".join(lines)
    write_or_print(text, args.report)

    return 0

def move_performance_slot_to_empty(
    source: bytes,
    source_slot: int,
    target_slot: int,
) -> tuple[bytes, int, int, bytes, bytes, int, int, bytes]:
    if source_slot == target_slot:
        raise ValueError("source and target performance slots must be different")

    source_offset, source_slot_data = read_complete_slot(
        source,
        PERFORMANCE_AREA_START,
        PERFORMANCE_SLOT_SIZE,
        PERFORMANCE_SLOT_COUNT,
        source_slot,
        "performance",
    )

    target_offset, old_target_slot_data = read_complete_slot(
        source,
        PERFORMANCE_AREA_START,
        PERFORMANCE_SLOT_SIZE,
        PERFORMANCE_SLOT_COUNT,
        target_slot,
        "performance",
    )

    template_slot, template_offset, template_slot_data = find_initial_slot_template(
        source,
        PERFORMANCE_AREA_START,
        PERFORMANCE_SLOT_SIZE,
        PERFORMANCE_NAME_BYTES,
        PERFORMANCE_SLOT_COUNT,
        "performance",
    )

    if source_slot_data == template_slot_data:
        raise ValueError("source performance slot is already INITIAL DATA")

    if old_target_slot_data != template_slot_data:
        raise ValueError(
            "target performance slot is not INITIAL DATA; use Swap, Copy, or Clear first"
        )

    output = bytearray(source)
    output[target_offset:target_offset + PERFORMANCE_SLOT_SIZE] = source_slot_data
    output[source_offset:source_offset + PERFORMANCE_SLOT_SIZE] = template_slot_data

    return (
        bytes(output),
        source_offset,
        target_offset,
        source_slot_data,
        old_target_slot_data,
        template_slot,
        template_offset,
        template_slot_data,
    )


def move_patch_slot_to_empty(
    source: bytes,
    source_slot: int,
    target_slot: int,
) -> tuple[bytes, int, int, bytes, bytes, int, int, bytes]:
    if source_slot == target_slot:
        raise ValueError("source and target patch slots must be different")

    source_offset, source_slot_data = read_complete_slot(
        source,
        PATCH_AREA_START,
        PATCH_SLOT_SIZE,
        PATCH_SLOT_COUNT,
        source_slot,
        "patch",
    )

    target_offset, old_target_slot_data = read_complete_slot(
        source,
        PATCH_AREA_START,
        PATCH_SLOT_SIZE,
        PATCH_SLOT_COUNT,
        target_slot,
        "patch",
    )

    template_slot, template_offset, template_slot_data = find_initial_slot_template(
        source,
        PATCH_AREA_START,
        PATCH_SLOT_SIZE,
        PATCH_NAME_BYTES,
        PATCH_SLOT_COUNT,
        "patch",
    )

    if source_slot_data == template_slot_data:
        raise ValueError("source patch slot is already INITIAL DATA")

    if old_target_slot_data != template_slot_data:
        raise ValueError(
            "target patch slot is not INITIAL DATA; use Swap, Copy, or Clear first"
        )

    output = bytearray(source)
    output[target_offset:target_offset + PATCH_SLOT_SIZE] = source_slot_data
    output[source_offset:source_offset + PATCH_SLOT_SIZE] = template_slot_data

    return (
        bytes(output),
        source_offset,
        target_offset,
        source_slot_data,
        old_target_slot_data,
        template_slot,
        template_offset,
        template_slot_data,
    )


def command_move_performance_to_empty(args) -> int:
    source_path = Path(args.source)
    output_path = Path(args.output)

    try:
        source = read_file(source_path)
        require_card(source_path, source)
        validate_output_path(output_path, [source_path])

        (
            output,
            source_offset,
            target_offset,
            source_slot_data,
            old_target_slot_data,
            template_slot,
            template_offset,
            template_slot_data,
        ) = move_performance_slot_to_empty(
            source,
            args.source_slot,
            args.target_slot,
        )

        output_path.write_bytes(output)

    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    new_source_slot_data = output[
        source_offset:
        source_offset + PERFORMANCE_SLOT_SIZE
    ]
    new_target_slot_data = output[
        target_offset:
        target_offset + PERFORMANCE_SLOT_SIZE
    ]

    source_name = slot_display_name(source_slot_data, PERFORMANCE_NAME_BYTES)
    old_target_name = slot_display_name(old_target_slot_data, PERFORMANCE_NAME_BYTES)
    new_source_name = slot_display_name(new_source_slot_data, PERFORMANCE_NAME_BYTES)
    new_target_name = slot_display_name(new_target_slot_data, PERFORMANCE_NAME_BYTES)
    template_name = slot_display_name(template_slot_data, PERFORMANCE_NAME_BYTES)

    lines = [
        "MiniJV880 CardRAM move performance to empty completed",
        "====================================================",
        "",
        f"Source file: {source_path}",
        f"Output file: {output_path}",
        "",
        "Performance move:",
        f"  source slot:       {args.source_slot}",
        f"  source offset:     0x{source_offset:04X}",
        f"  old source name:   {source_name}",
        f"  new source name:   {new_source_name}",
        f"  old source digest: 0x{fnv1a32(source_slot_data):08X}",
        f"  new source digest: 0x{fnv1a32(new_source_slot_data):08X}",
        "",
        f"  target slot:       {args.target_slot}",
        f"  target offset:     0x{target_offset:04X}",
        f"  old target name:   {old_target_name}",
        f"  new target name:   {new_target_name}",
        f"  old target digest: 0x{fnv1a32(old_target_slot_data):08X}",
        f"  new target digest: 0x{fnv1a32(new_target_slot_data):08X}",
        "",
        "Initial template:",
        f"  template slot:     {template_slot}",
        f"  template offset:   0x{template_offset:04X}",
        f"  template name:     {template_name}",
        f"  template digest:   0x{fnv1a32(template_slot_data):08X}",
        "",
        f"Source digest: 0x{fnv1a32(source):08X}",
        f"Output digest: 0x{fnv1a32(output):08X}",
        "",
        "Original file was not modified.",
        "Move was allowed only because target slot was INITIAL DATA.",
        "",
    ]

    text = "\n".join(lines)
    write_or_print(text, args.report)

    return 0


def command_move_patch_to_empty(args) -> int:
    source_path = Path(args.source)
    output_path = Path(args.output)

    try:
        source = read_file(source_path)
        require_card(source_path, source)
        validate_output_path(output_path, [source_path])

        (
            output,
            source_offset,
            target_offset,
            source_slot_data,
            old_target_slot_data,
            template_slot,
            template_offset,
            template_slot_data,
        ) = move_patch_slot_to_empty(
            source,
            args.source_slot,
            args.target_slot,
        )

        output_path.write_bytes(output)

    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    new_source_slot_data = output[
        source_offset:
        source_offset + PATCH_SLOT_SIZE
    ]
    new_target_slot_data = output[
        target_offset:
        target_offset + PATCH_SLOT_SIZE
    ]

    source_name = slot_display_name(source_slot_data, PATCH_NAME_BYTES)
    old_target_name = slot_display_name(old_target_slot_data, PATCH_NAME_BYTES)
    new_source_name = slot_display_name(new_source_slot_data, PATCH_NAME_BYTES)
    new_target_name = slot_display_name(new_target_slot_data, PATCH_NAME_BYTES)
    template_name = slot_display_name(template_slot_data, PATCH_NAME_BYTES)

    lines = [
        "MiniJV880 CardRAM move patch to empty completed",
        "===============================================",
        "",
        f"Source file: {source_path}",
        f"Output file: {output_path}",
        "",
        "Patch move:",
        f"  source slot:       {args.source_slot}",
        f"  source offset:     0x{source_offset:04X}",
        f"  old source name:   {source_name}",
        f"  new source name:   {new_source_name}",
        f"  old source digest: 0x{fnv1a32(source_slot_data):08X}",
        f"  new source digest: 0x{fnv1a32(new_source_slot_data):08X}",
        "",
        f"  target slot:       {args.target_slot}",
        f"  target offset:     0x{target_offset:04X}",
        f"  old target name:   {old_target_name}",
        f"  new target name:   {new_target_name}",
        f"  old target digest: 0x{fnv1a32(old_target_slot_data):08X}",
        f"  new target digest: 0x{fnv1a32(new_target_slot_data):08X}",
        "",
        "Initial template:",
        f"  template slot:     {template_slot}",
        f"  template offset:   0x{template_offset:04X}",
        f"  template name:     {template_name}",
        f"  template digest:   0x{fnv1a32(template_slot_data):08X}",
        "",
        f"Source digest: 0x{fnv1a32(source):08X}",
        f"Output digest: 0x{fnv1a32(output):08X}",
        "",
        "Original file was not modified.",
        "Move was allowed only because target slot was INITIAL DATA.",
        "",
    ]

    text = "\n".join(lines)
    write_or_print(text, args.report)

    return 0

def copy_performance_slot(
    source: bytes,
    dest: bytes,
    source_slot: int,
    target_slot: int,
) -> tuple[bytes, int, int, bytes, bytes]:
    if source_slot < 1 or source_slot > PERFORMANCE_SLOT_COUNT:
        raise ValueError(f"source performance slot out of range: {source_slot}")

    if target_slot < 1 or target_slot > PERFORMANCE_SLOT_COUNT:
        raise ValueError(f"target performance slot out of range: {target_slot}")

    source_offset = PERFORMANCE_AREA_START + (source_slot - 1) * PERFORMANCE_SLOT_SIZE
    target_offset = PERFORMANCE_AREA_START + (target_slot - 1) * PERFORMANCE_SLOT_SIZE

    source_slot_data = source[source_offset:source_offset + PERFORMANCE_SLOT_SIZE]
    old_target_slot_data = dest[target_offset:target_offset + PERFORMANCE_SLOT_SIZE]

    if len(source_slot_data) != PERFORMANCE_SLOT_SIZE:
        raise ValueError("source performance slot is incomplete")

    if len(old_target_slot_data) != PERFORMANCE_SLOT_SIZE:
        raise ValueError("target performance slot is incomplete")

    output = bytearray(dest)
    output[target_offset:target_offset + PERFORMANCE_SLOT_SIZE] = source_slot_data

    return bytes(output), source_offset, target_offset, source_slot_data, old_target_slot_data


def command_copy_performance(args) -> int:
    source_path = Path(args.source)
    dest_path = Path(args.dest)
    output_path = Path(args.output)

    try:
        source = read_file(source_path)
        dest = read_file(dest_path)
        require_card(source_path, source)
        require_card(dest_path, dest)
        validate_output_path(output_path, [source_path, dest_path])

        output, source_offset, target_offset, source_slot_data, old_target_slot_data = copy_performance_slot(
            source,
            dest,
            args.source_slot,
            args.target_slot,
        )

        output_path.write_bytes(output)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    source_name = decode_patch_name(source_slot_data[:PERFORMANCE_NAME_BYTES])
    old_target_name = decode_patch_name(old_target_slot_data[:PERFORMANCE_NAME_BYTES])
    new_target_data = output[target_offset:target_offset + PERFORMANCE_SLOT_SIZE]
    new_target_name = decode_patch_name(new_target_data[:PERFORMANCE_NAME_BYTES])

    if source_name == "":
        source_name = "<blank>"

    if old_target_name == "":
        old_target_name = "<blank>"

    if new_target_name == "":
        new_target_name = "<blank>"

    lines = [
        "MiniJV880 CardRAM copy performance completed",
        "============================================",
        "",
        f"Source file:      {source_path}",
        f"Destination file: {dest_path}",
        f"Output file:      {output_path}",
        "",
        "Performance copy:",
        f"  source slot:    {args.source_slot}",
        f"  source offset:  0x{source_offset:04X}",
        f"  source name:    {source_name}",
        f"  source digest:  0x{fnv1a32(source_slot_data):08X}",
        "",
        f"  target slot:    {args.target_slot}",
        f"  target offset:  0x{target_offset:04X}",
        f"  old name:       {old_target_name}",
        f"  old digest:     0x{fnv1a32(old_target_slot_data):08X}",
        f"  new name:       {new_target_name}",
        f"  new digest:     0x{fnv1a32(new_target_data):08X}",
        "",
        f"Output digest:    0x{fnv1a32(output):08X}",
        "",
        "Original files were not modified.",
        "",
    ]

    text = "\n".join(lines)
    write_or_print(text, args.report)

    return 0


def copy_performance_bank_area(
    source: bytes,
    dest: bytes,
) -> tuple[bytes, int, bytes, bytes]:
    source_offset = PERFORMANCE_AREA_START
    performance_area_size = PERFORMANCE_SLOT_COUNT * PERFORMANCE_SLOT_SIZE

    source_performance_data = source[
        PERFORMANCE_AREA_START:PERFORMANCE_AREA_START + performance_area_size
    ]
    old_target_performance_data = dest[
        PERFORMANCE_AREA_START:PERFORMANCE_AREA_START + performance_area_size
    ]

    if len(source_performance_data) != performance_area_size:
        raise ValueError("source performance bank area is incomplete")

    if len(old_target_performance_data) != performance_area_size:
        raise ValueError("target performance bank area is incomplete")

    output = bytearray(dest)
    output[
        PERFORMANCE_AREA_START:PERFORMANCE_AREA_START + performance_area_size
    ] = source_performance_data

    return bytes(output), source_offset, source_performance_data, old_target_performance_data


def command_copy_performance_bank(args) -> int:
    source_path = Path(args.source)
    dest_path = Path(args.dest)
    output_path = Path(args.output)

    try:
        source = read_file(source_path)
        dest = read_file(dest_path)
        require_card(source_path, source)
        require_card(dest_path, dest)
        validate_output_path(output_path, [source_path, dest_path])

        output, source_offset, source_performance_data, old_target_performance_data = copy_performance_bank_area(
            source,
            dest,
        )

        output_path.write_bytes(output)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    performance_area_size = PERFORMANCE_SLOT_COUNT * PERFORMANCE_SLOT_SIZE
    new_target_performance_data = output[
        PERFORMANCE_AREA_START:PERFORMANCE_AREA_START + performance_area_size
    ]

    lines = [
        "MiniJV880 CardRAM copy performance bank completed",
        "=================================================",
        "",
        f"Source file:      {source_path}",
        f"Destination file: {dest_path}",
        f"Output file:      {output_path}",
        "",
        "Performance bank copy:",
        f"  source offset:  0x{source_offset:04X}",
        f"  area size:      {performance_area_size} bytes",
        f"  area end:       0x{PERFORMANCE_AREA_START + performance_area_size - 1:04X}",
        f"  slots copied:   {PERFORMANCE_SLOT_COUNT}",
        f"  source digest:  0x{fnv1a32(source_performance_data):08X}",
        "",
        f"  target offset:  0x{PERFORMANCE_AREA_START:04X}",
        f"  old digest:     0x{fnv1a32(old_target_performance_data):08X}",
        f"  new digest:     0x{fnv1a32(new_target_performance_data):08X}",
        "",
        f"Output digest:    0x{fnv1a32(output):08X}",
        "",
        "Original files were not modified.",
        "Performance bank was copied as 16 raw performance slots.",
        "",
    ]

    text = "\n".join(lines)
    write_or_print(text, args.report)

    return 0


def copy_patch_slot(
    source: bytes,
    dest: bytes,
    source_slot: int,
    target_slot: int,
) -> tuple[bytes, int, int, bytes, bytes]:
    if source_slot < 1 or source_slot > PATCH_SLOT_COUNT:
        raise ValueError(f"source patch slot out of range: {source_slot}")

    if target_slot < 1 or target_slot > PATCH_SLOT_COUNT:
        raise ValueError(f"target patch slot out of range: {target_slot}")

    source_offset = PATCH_AREA_START + (source_slot - 1) * PATCH_SLOT_SIZE
    target_offset = PATCH_AREA_START + (target_slot - 1) * PATCH_SLOT_SIZE

    source_slot_data = source[source_offset:source_offset + PATCH_SLOT_SIZE]
    old_target_slot_data = dest[target_offset:target_offset + PATCH_SLOT_SIZE]

    if len(source_slot_data) != PATCH_SLOT_SIZE:
        raise ValueError("source patch slot is incomplete")

    if len(old_target_slot_data) != PATCH_SLOT_SIZE:
        raise ValueError("target patch slot is incomplete")

    output = bytearray(dest)
    output[target_offset:target_offset + PATCH_SLOT_SIZE] = source_slot_data

    return bytes(output), source_offset, target_offset, source_slot_data, old_target_slot_data


def command_copy_patch(args) -> int:
    source_path = Path(args.source)
    dest_path = Path(args.dest)
    output_path = Path(args.output)

    try:
        source = read_file(source_path)
        dest = read_file(dest_path)
        require_card(source_path, source)
        require_card(dest_path, dest)
        validate_output_path(output_path, [source_path, dest_path])

        output, source_offset, target_offset, source_slot_data, old_target_slot_data = copy_patch_slot(
            source,
            dest,
            args.source_slot,
            args.target_slot,
        )

        output_path.write_bytes(output)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    source_name = decode_patch_name(source_slot_data[:PATCH_NAME_BYTES])
    old_target_name = decode_patch_name(old_target_slot_data[:PATCH_NAME_BYTES])
    new_target_data = output[target_offset:target_offset + PATCH_SLOT_SIZE]
    new_target_name = decode_patch_name(new_target_data[:PATCH_NAME_BYTES])

    if source_name == "":
        source_name = "<blank>"

    if old_target_name == "":
        old_target_name = "<blank>"

    if new_target_name == "":
        new_target_name = "<blank>"

    lines = [
        "MiniJV880 CardRAM copy patch completed",
        "======================================",
        "",
        f"Source file:      {source_path}",
        f"Destination file: {dest_path}",
        f"Output file:      {output_path}",
        "",
        "Patch copy:",
        f"  source slot:    {args.source_slot}",
        f"  source offset:  0x{source_offset:04X}",
        f"  source name:    {source_name}",
        f"  source digest:  0x{fnv1a32(source_slot_data):08X}",
        "",
        f"  target slot:    {args.target_slot}",
        f"  target offset:  0x{target_offset:04X}",
        f"  old name:       {old_target_name}",
        f"  old digest:     0x{fnv1a32(old_target_slot_data):08X}",
        f"  new name:       {new_target_name}",
        f"  new digest:     0x{fnv1a32(new_target_data):08X}",
        "",
        f"Output digest:    0x{fnv1a32(output):08X}",
        "",
        "Original files were not modified.",
        "",
    ]

    text = "\n".join(lines)
    write_or_print(text, args.report)

    return 0


def copy_patch_bank_area(
    source: bytes,
    dest: bytes,
) -> tuple[bytes, int, bytes, bytes]:
    source_offset = PATCH_AREA_START
    patch_area_size = PATCH_SLOT_COUNT * PATCH_SLOT_SIZE

    source_patch_data = source[PATCH_AREA_START:PATCH_AREA_START + patch_area_size]
    old_target_patch_data = dest[PATCH_AREA_START:PATCH_AREA_START + patch_area_size]

    if len(source_patch_data) != patch_area_size:
        raise ValueError("source patch bank area is incomplete")

    if len(old_target_patch_data) != patch_area_size:
        raise ValueError("target patch bank area is incomplete")

    output = bytearray(dest)
    output[PATCH_AREA_START:PATCH_AREA_START + patch_area_size] = source_patch_data

    return bytes(output), source_offset, source_patch_data, old_target_patch_data


def command_copy_patch_bank(args) -> int:
    source_path = Path(args.source)
    dest_path = Path(args.dest)
    output_path = Path(args.output)

    try:
        source = read_file(source_path)
        dest = read_file(dest_path)
        require_card(source_path, source)
        require_card(dest_path, dest)
        validate_output_path(output_path, [source_path, dest_path])

        output, source_offset, source_patch_data, old_target_patch_data = copy_patch_bank_area(
            source,
            dest,
        )

        output_path.write_bytes(output)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    new_target_patch_data = output[PATCH_AREA_START:PATCH_AREA_START + PATCH_SLOT_COUNT * PATCH_SLOT_SIZE]

    lines = [
        "MiniJV880 CardRAM copy patch bank completed",
        "===========================================",
        "",
        f"Source file:      {source_path}",
        f"Destination file: {dest_path}",
        f"Output file:      {output_path}",
        "",
        "Patch bank copy:",
        f"  source offset:  0x{source_offset:04X}",
        f"  area size:      {PATCH_SLOT_COUNT * PATCH_SLOT_SIZE} bytes",
        f"  area end:       0x{PATCH_AREA_START + PATCH_SLOT_COUNT * PATCH_SLOT_SIZE - 1:04X}",
        f"  slots copied:   {PATCH_SLOT_COUNT}",
        f"  source digest:  0x{fnv1a32(source_patch_data):08X}",
        "",
        f"  target offset:  0x{PATCH_AREA_START:04X}",
        f"  old digest:     0x{fnv1a32(old_target_patch_data):08X}",
        f"  new digest:     0x{fnv1a32(new_target_patch_data):08X}",
        "",
        f"Output digest:    0x{fnv1a32(output):08X}",
        "",
        "Original files were not modified.",
        "Patch bank was copied as 64 raw patch slots.",
        "",
    ]

    text = "\n".join(lines)
    write_or_print(text, args.report)

    return 0


def copy_rhythm_area(
    source: bytes,
    dest: bytes,
) -> tuple[bytes, int, bytes, bytes]:
    source_offset = RHYTHM_AREA_START

    source_rhythm_data = source[RHYTHM_AREA_START:RHYTHM_AREA_START + RHYTHM_AREA_SIZE]
    old_target_rhythm_data = dest[RHYTHM_AREA_START:RHYTHM_AREA_START + RHYTHM_AREA_SIZE]

    if len(source_rhythm_data) != RHYTHM_AREA_SIZE:
        raise ValueError("source rhythm area is incomplete")

    if len(old_target_rhythm_data) != RHYTHM_AREA_SIZE:
        raise ValueError("target rhythm area is incomplete")

    output = bytearray(dest)
    output[RHYTHM_AREA_START:RHYTHM_AREA_START + RHYTHM_AREA_SIZE] = source_rhythm_data

    return bytes(output), source_offset, source_rhythm_data, old_target_rhythm_data


def command_copy_rhythm(args) -> int:
    source_path = Path(args.source)
    dest_path = Path(args.dest)
    output_path = Path(args.output)

    try:
        source = read_file(source_path)
        dest = read_file(dest_path)
        require_card(source_path, source)
        require_card(dest_path, dest)
        validate_output_path(output_path, [source_path, dest_path])

        output, source_offset, source_rhythm_data, old_target_rhythm_data = copy_rhythm_area(
            source,
            dest,
        )

        output_path.write_bytes(output)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    new_target_rhythm_data = output[RHYTHM_AREA_START:RHYTHM_AREA_START + RHYTHM_AREA_SIZE]

    lines = [
        "MiniJV880 CardRAM copy rhythm completed",
        "=======================================",
        "",
        f"Source file:      {source_path}",
        f"Destination file: {dest_path}",
        f"Output file:      {output_path}",
        "",
        "Rhythm copy:",
        f"  source offset:  0x{source_offset:04X}",
        f"  area size:      {RHYTHM_AREA_SIZE} bytes",
        f"  area end:       0x{RHYTHM_AREA_START + RHYTHM_AREA_SIZE - 1:04X}",
        f"  source digest:  0x{fnv1a32(source_rhythm_data):08X}",
        "",
        f"  target offset:  0x{RHYTHM_AREA_START:04X}",
        f"  old digest:     0x{fnv1a32(old_target_rhythm_data):08X}",
        f"  new digest:     0x{fnv1a32(new_target_rhythm_data):08X}",
        "",
        f"Output digest:    0x{fnv1a32(output):08X}",
        "",
        "Original files were not modified.",
        "Rhythm area was copied as raw data; its internal structure is not decoded.",
        "",
    ]

    text = "\n".join(lines)
    write_or_print(text, args.report)

    return 0


def command_compose_card(args) -> int:
    base_path = Path(args.base)
    output_path = Path(args.output)

    performance_source_path = Path(args.performance_source) if args.performance_source else None
    patch_source_path = Path(args.patch_source) if args.patch_source else None
    rhythm_source_path = Path(args.rhythm_source) if args.rhythm_source else None

    if (
        performance_source_path is None and
        patch_source_path is None and
        rhythm_source_path is None
    ):
        print("ERROR: at least one of --performance-source, --patch-source or --rhythm-source is required", file=sys.stderr)
        return 2

    input_paths = [base_path]

    if performance_source_path is not None:
        input_paths.append(performance_source_path)

    if patch_source_path is not None:
        input_paths.append(patch_source_path)

    if rhythm_source_path is not None:
        input_paths.append(rhythm_source_path)

    try:
        base = read_file(base_path)
        require_card(base_path, base)

        validate_output_path(output_path, input_paths)

        output = bytes(base)

        lines = [
            "MiniJV880 CardRAM compose completed",
            "===================================",
            "",
            f"Base file:   {base_path}",
            f"Output file: {output_path}",
            "",
            f"Base digest: 0x{fnv1a32(base):08X}",
            "",
            "Applied areas:",
        ]

        if performance_source_path is not None:
            performance_source = read_file(performance_source_path)
            require_card(performance_source_path, performance_source)

            output, source_offset, source_data, old_target_data = copy_performance_bank_area(
                performance_source,
                output,
            )

            new_data = output[
                PERFORMANCE_AREA_START:
                PERFORMANCE_AREA_START + PERFORMANCE_SLOT_COUNT * PERFORMANCE_SLOT_SIZE
            ]

            lines.extend([
                "",
                "  Performance bank:",
                f"    source file:   {performance_source_path}",
                f"    area start:    0x{source_offset:04X}",
                f"    area size:     {PERFORMANCE_SLOT_COUNT * PERFORMANCE_SLOT_SIZE} bytes",
                f"    source digest: 0x{fnv1a32(source_data):08X}",
                f"    old digest:    0x{fnv1a32(old_target_data):08X}",
                f"    new digest:    0x{fnv1a32(new_data):08X}",
            ])
        else:
            lines.extend([
                "",
                "  Performance bank: kept from base",
            ])

        if patch_source_path is not None:
            patch_source = read_file(patch_source_path)
            require_card(patch_source_path, patch_source)

            output, source_offset, source_data, old_target_data = copy_patch_bank_area(
                patch_source,
                output,
            )

            new_data = output[
                PATCH_AREA_START:
                PATCH_AREA_START + PATCH_SLOT_COUNT * PATCH_SLOT_SIZE
            ]

            lines.extend([
                "",
                "  Patch bank:",
                f"    source file:   {patch_source_path}",
                f"    area start:    0x{source_offset:04X}",
                f"    area size:     {PATCH_SLOT_COUNT * PATCH_SLOT_SIZE} bytes",
                f"    source digest: 0x{fnv1a32(source_data):08X}",
                f"    old digest:    0x{fnv1a32(old_target_data):08X}",
                f"    new digest:    0x{fnv1a32(new_data):08X}",
            ])
        else:
            lines.extend([
                "",
                "  Patch bank: kept from base",
            ])

        if rhythm_source_path is not None:
            rhythm_source = read_file(rhythm_source_path)
            require_card(rhythm_source_path, rhythm_source)

            output, source_offset, source_data, old_target_data = copy_rhythm_area(
                rhythm_source,
                output,
            )

            new_data = output[
                RHYTHM_AREA_START:
                RHYTHM_AREA_START + RHYTHM_AREA_SIZE
            ]

            lines.extend([
                "",
                "  Rhythm area:",
                f"    source file:   {rhythm_source_path}",
                f"    area start:    0x{source_offset:04X}",
                f"    area size:     {RHYTHM_AREA_SIZE} bytes",
                f"    source digest: 0x{fnv1a32(source_data):08X}",
                f"    old digest:    0x{fnv1a32(old_target_data):08X}",
                f"    new digest:    0x{fnv1a32(new_data):08X}",
            ])
        else:
            lines.extend([
                "",
                "  Rhythm area: kept from base",
            ])

        output_path.write_bytes(output)

    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    lines.extend([
        "",
        f"Output digest: 0x{fnv1a32(output):08X}",
        "",
        "Original files were not modified.",
        "",
    ])

    text = "\n".join(lines)
    write_or_print(text, args.report)

    return 0


def collect_named_slots(
    data: bytes,
    area_name: str,
    area_start: int,
    slot_size: int,
    name_bytes: int,
    slot_count: int,
) -> list[tuple[str, int, int, str, int]]:
    results = []

    for slot in range(slot_count):
        offset = area_start + slot * slot_size
        slot_data = data[offset:offset + slot_size]

        if len(slot_data) != slot_size:
            break

        name = decode_patch_name(slot_data[:name_bytes])
        if name == "":
            name = "<blank>"

        results.append((area_name, slot + 1, offset, name, fnv1a32(slot_data)))

    return results


def render_find_name_report(
    path: Path,
    data: bytes,
    query: str,
    exact: bool,
) -> str:
    query_cmp = query.lower()

    all_slots = []
    all_slots.extend(
        collect_named_slots(
            data,
            "Performance",
            PERFORMANCE_AREA_START,
            PERFORMANCE_SLOT_SIZE,
            PERFORMANCE_NAME_BYTES,
            PERFORMANCE_SLOT_COUNT,
        )
    )
    all_slots.extend(
        collect_named_slots(
            data,
            "Patch",
            PATCH_AREA_START,
            PATCH_SLOT_SIZE,
            PATCH_NAME_BYTES,
            PATCH_SLOT_COUNT,
        )
    )

    matches = []

    for area_name, slot, offset, name, digest in all_slots:
        name_cmp = name.lower()

        if exact:
            matched = name_cmp == query_cmp
        else:
            matched = query_cmp in name_cmp

        if matched:
            matches.append((area_name, slot, offset, name, digest))

    lines = [
        "MiniJV880 CardRAM name search",
        "=============================",
        "",
        f"File:   {path.name}",
        f"Path:   {path.resolve()}",
        f"Size:   {len(data)} bytes",
        f"Digest: 0x{fnv1a32(data):08X}",
        "",
        "Search:",
        f"  query: {query}",
        f"  mode:  {'exact' if exact else 'contains'}",
        "",
        "Matches:",
        "  Area         Slot  Offset   Name          Slot digest",
        "  -----------  ----  -------  ------------  -----------",
    ]

    for area_name, slot, offset, name, digest in matches:
        lines.append(
            f"  {area_name:<11}  {slot:04d}  0x{offset:04X}   {name:<12.12}  0x{digest:08X}"
        )

    if not matches:
        lines.append("  No matches found.")

    lines.extend([
        "",
        f"Total matches: {len(matches)}",
        "",
    ])

    return "\n".join(lines)


def command_find_name(args) -> int:
    path = Path(args.file)

    try:
        data = read_file(path)
        require_card(path, data)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    text = render_find_name_report(
        path,
        data,
        args.query,
        args.exact,
    )

    write_or_print(text, args.report)

    return 0


def count_non_printable_name_fields(
    data: bytes,
    area_start: int,
    slot_size: int,
    name_bytes: int,
    slot_count: int,
) -> list[tuple[int, int, str]]:
    issues = []

    for slot in range(slot_count):
        offset = area_start + slot * slot_size
        raw_name = data[offset:offset + name_bytes]

        if len(raw_name) != name_bytes:
            issues.append((slot + 1, offset, "incomplete name field"))
            continue

        for value in raw_name:
            if not is_printable_ascii_byte(value):
                issues.append((slot + 1, offset, "non-printable byte in name field"))
                break

    return issues


def find_duplicate_names(
    named_slots: list[tuple[str, int, int, str, int]]
) -> list[tuple[str, list[int]]]:
    by_name = {}

    for _area_name, slot, _offset, name, _digest in named_slots:
        by_name.setdefault(name, []).append(slot)

    duplicates = []

    for name, slots in by_name.items():
        if name != "<blank>" and len(slots) > 1:
            duplicates.append((name, slots))

    return duplicates


def render_check_card_report(path: Path, data: bytes) -> tuple[str, bool]:
    performance_area_size = PERFORMANCE_SLOT_COUNT * PERFORMANCE_SLOT_SIZE
    patch_area_size = PATCH_SLOT_COUNT * PATCH_SLOT_SIZE

    header_data = data[0:PERFORMANCE_AREA_START]
    performance_data = data[
        PERFORMANCE_AREA_START:PERFORMANCE_AREA_START + performance_area_size
    ]
    patch_data = data[
        PATCH_AREA_START:PATCH_AREA_START + patch_area_size
    ]
    rhythm_data = data[
        RHYTHM_AREA_START:RHYTHM_AREA_START + RHYTHM_AREA_SIZE
    ]

    performance_slots = collect_named_slots(
        data,
        "Performance",
        PERFORMANCE_AREA_START,
        PERFORMANCE_SLOT_SIZE,
        PERFORMANCE_NAME_BYTES,
        PERFORMANCE_SLOT_COUNT,
    )
    patch_slots = collect_named_slots(
        data,
        "Patch",
        PATCH_AREA_START,
        PATCH_SLOT_SIZE,
        PATCH_NAME_BYTES,
        PATCH_SLOT_COUNT,
    )

    performance_name_issues = count_non_printable_name_fields(
        data,
        PERFORMANCE_AREA_START,
        PERFORMANCE_SLOT_SIZE,
        PERFORMANCE_NAME_BYTES,
        PERFORMANCE_SLOT_COUNT,
    )
    patch_name_issues = count_non_printable_name_fields(
        data,
        PATCH_AREA_START,
        PATCH_SLOT_SIZE,
        PATCH_NAME_BYTES,
        PATCH_SLOT_COUNT,
    )

    performance_duplicates = find_duplicate_names(performance_slots)
    patch_duplicates = find_duplicate_names(patch_slots)

    ok = (
        len(data) == CARD_SIZE and
        len(header_data) == PERFORMANCE_AREA_START and
        len(performance_data) == performance_area_size and
        len(patch_data) == patch_area_size and
        len(rhythm_data) == RHYTHM_AREA_SIZE and
        len(performance_slots) == PERFORMANCE_SLOT_COUNT and
        len(patch_slots) == PATCH_SLOT_COUNT and
        not performance_name_issues and
        not patch_name_issues
    )

    lines = [
        "MiniJV880 CardRAM check",
        "=======================",
        "",
        f"File:   {path.name}",
        f"Path:   {path.resolve()}",
        f"Size:   {len(data)} bytes",
        f"Digest: 0x{fnv1a32(data):08X}",
        f"Status: {'OK' if ok else 'WARN'}",
        "",
        "Area checks:",
        f"  Header:      {'OK' if len(header_data) == PERFORMANCE_AREA_START else 'WARN'}  0x0000 - 0x001F   {len(header_data)} bytes   digest 0x{fnv1a32(header_data):08X}",
        f"  Performance: {'OK' if len(performance_data) == performance_area_size else 'WARN'}  0x{PERFORMANCE_AREA_START:04X} - 0x{PERFORMANCE_AREA_START + performance_area_size - 1:04X}   {len(performance_data)} bytes   digest 0x{fnv1a32(performance_data):08X}",
        f"  Patch:       {'OK' if len(patch_data) == patch_area_size else 'WARN'}  0x{PATCH_AREA_START:04X} - 0x{PATCH_AREA_START + patch_area_size - 1:04X}   {len(patch_data)} bytes   digest 0x{fnv1a32(patch_data):08X}",
        f"  Rhythm:      {'OK' if len(rhythm_data) == RHYTHM_AREA_SIZE else 'WARN'}  0x{RHYTHM_AREA_START:04X} - 0x{RHYTHM_AREA_START + RHYTHM_AREA_SIZE - 1:04X}   {len(rhythm_data)} bytes   digest 0x{fnv1a32(rhythm_data):08X}",
        "",
        "Slot checks:",
        f"  Performance slots found: {len(performance_slots)}/{PERFORMANCE_SLOT_COUNT}",
        f"  Patch slots found:       {len(patch_slots)}/{PATCH_SLOT_COUNT}",
        "",
        "Name field checks:",
        f"  Performance non-printable/incomplete name fields: {len(performance_name_issues)}",
        f"  Patch non-printable/incomplete name fields:       {len(patch_name_issues)}",
        "",
    ]

    if performance_name_issues or patch_name_issues:
        lines.append("Name field issues:")

        for slot, offset, message in performance_name_issues:
            lines.append(f"  Performance {slot:03d} at 0x{offset:04X}: {message}")

        for slot, offset, message in patch_name_issues:
            lines.append(f"  Patch {slot:03d} at 0x{offset:04X}: {message}")

        lines.append("")

    lines.append("Duplicate names inside same area:")

    if not performance_duplicates and not patch_duplicates:
        lines.append("  none")
    else:
        for name, slots in performance_duplicates:
            slot_text = ", ".join(str(slot) for slot in slots)
            lines.append(f"  Performance name '{name}' appears in slots: {slot_text}")

        for name, slots in patch_duplicates:
            slot_text = ", ".join(str(slot) for slot in slots)
            lines.append(f"  Patch name '{name}' appears in slots: {slot_text}")

    lines.append("")

    return "\n".join(lines), ok


def command_check_card(args) -> int:
    path = Path(args.file)

    try:
        data = read_file(path)
        require_card(path, data)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    text, ok = render_check_card_report(path, data)
    write_or_print(text, args.report)

    return 0 if ok else 1


def get_slot_layout(area: str) -> tuple[str, int, int, int, int]:
    if area == "performance":
        return (
            "Performance",
            PERFORMANCE_AREA_START,
            PERFORMANCE_SLOT_SIZE,
            PERFORMANCE_NAME_BYTES,
            PERFORMANCE_SLOT_COUNT,
        )

    if area == "patch":
        return (
            "Patch",
            PATCH_AREA_START,
            PATCH_SLOT_SIZE,
            PATCH_NAME_BYTES,
            PATCH_SLOT_COUNT,
        )

    raise ValueError(f"unknown area: {area}")


def render_slot_info_report(
    path: Path,
    data: bytes,
    area: str,
    slot: int,
    preview_bytes: int,
) -> str:
    area_label, area_start, slot_size, name_bytes, slot_count = get_slot_layout(area)

    if slot < 1 or slot > slot_count:
        raise ValueError(f"{area} slot must be between 1 and {slot_count}")

    offset = area_start + (slot - 1) * slot_size
    slot_data = data[offset:offset + slot_size]

    if len(slot_data) != slot_size:
        raise ValueError(f"{area} slot is incomplete")

    preview_len = min(preview_bytes, slot_size)
    name = decode_patch_name(slot_data[:name_bytes])

    if name == "":
        name = "<blank>"

    lines = [
        "MiniJV880 CardRAM slot info",
        "===========================",
        "",
        f"File:   {path.name}",
        f"Path:   {path.resolve()}",
        f"Size:   {len(data)} bytes",
        f"Digest: 0x{fnv1a32(data):08X}",
        "",
        "Slot:",
        f"  area:        {area_label}",
        f"  slot:        {slot}",
        f"  offset:      0x{offset:04X}",
        f"  size:        {slot_size} bytes",
        f"  name bytes:  {name_bytes}",
        f"  name:        {name}",
        f"  slot digest: 0x{fnv1a32(slot_data):08X}",
        "",
        f"Preview: first {preview_len} bytes",
        f"  hex:  {hex_bytes(slot_data, 0, preview_len)}",
        f"  text: {ascii_preview(slot_data, 0, preview_len)}",
        "",
    ]

    return "\n".join(lines)


def command_slot_info(args) -> int:
    path = Path(args.file)

    try:
        data = read_file(path)
        require_card(path, data)

        text = render_slot_info_report(
            path,
            data,
            args.area,
            args.slot,
            args.preview_bytes,
        )
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    write_or_print(text, args.report)

    return 0


def collect_all_named_slots(data: bytes) -> list[tuple[str, int, int, str, int]]:
    rows = []

    rows.extend(
        collect_named_slots(
            data,
            "Performance",
            PERFORMANCE_AREA_START,
            PERFORMANCE_SLOT_SIZE,
            PERFORMANCE_NAME_BYTES,
            PERFORMANCE_SLOT_COUNT,
        )
    )
    rows.extend(
        collect_named_slots(
            data,
            "Patch",
            PATCH_AREA_START,
            PATCH_SLOT_SIZE,
            PATCH_NAME_BYTES,
            PATCH_SLOT_COUNT,
        )
    )

    return rows


def render_compare_names_report(
    left_path: Path,
    right_path: Path,
    left: bytes,
    right: bytes,
) -> tuple[str, bool]:
    left_rows = collect_all_named_slots(left)
    right_rows = collect_all_named_slots(right)

    lines = [
        "MiniJV880 CardRAM name compare",
        "==============================",
        "",
        f"Left:   {left_path.name}",
        f"Path:   {left_path.resolve()}",
        f"Digest: 0x{fnv1a32(left):08X}",
        "",
        f"Right:  {right_path.name}",
        f"Path:   {right_path.resolve()}",
        f"Digest: 0x{fnv1a32(right):08X}",
        "",
        "Name differences:",
        "  Area         Slot  Offset   Left name     Right name",
        "  -----------  ----  -------  ------------  ------------",
    ]

    differences = []
    performance_differences = 0
    patch_differences = 0

    for left_row, right_row in zip(left_rows, right_rows):
        left_area, left_slot, left_offset, left_name, _left_digest = left_row
        right_area, right_slot, _right_offset, right_name, _right_digest = right_row

        if left_area != right_area or left_slot != right_slot:
            raise ValueError("internal slot layout mismatch while comparing names")

        if left_name != right_name:
            differences.append((left_area, left_slot, left_offset, left_name, right_name))

            if left_area == "Performance":
                performance_differences += 1
            elif left_area == "Patch":
                patch_differences += 1

    for area, slot, offset, left_name, right_name in differences:
        lines.append(
            f"  {area:<11}  {slot:04d}  0x{offset:04X}   "
            f"{left_name:<12.12}  {right_name:<12.12}"
        )

    if not differences:
        lines.append("  No name differences found.")

    lines.extend([
        "",
        f"Performance name differences: {performance_differences}",
        f"Patch name differences:       {patch_differences}",
        f"Total name differences:       {len(differences)}",
        "",
    ])

    return "\n".join(lines), len(differences) == 0


def command_compare_names(args) -> int:
    left_path = Path(args.left)
    right_path = Path(args.right)

    try:
        left = read_file(left_path)
        right = read_file(right_path)
        require_card(left_path, left)
        require_card(right_path, right)

        text, equal = render_compare_names_report(
            left_path,
            right_path,
            left,
            right,
        )
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    write_or_print(text, args.report)

    return 0 if equal else 1


def render_export_names_report(path: Path, data: bytes, delimiter: str) -> str:
    rows = []

    rows.extend(
        collect_named_slots(
            data,
            "Performance",
            PERFORMANCE_AREA_START,
            PERFORMANCE_SLOT_SIZE,
            PERFORMANCE_NAME_BYTES,
            PERFORMANCE_SLOT_COUNT,
        )
    )
    rows.extend(
        collect_named_slots(
            data,
            "Patch",
            PATCH_AREA_START,
            PATCH_SLOT_SIZE,
            PATCH_NAME_BYTES,
            PATCH_SLOT_COUNT,
        )
    )

    lines = [
        delimiter.join(["area", "slot", "offset", "name", "digest"])
    ]

    for area_name, slot, offset, name, digest in rows:
        lines.append(
            delimiter.join([
                area_name,
                str(slot),
                f"0x{offset:04X}",
                name,
                f"0x{digest:08X}",
            ])
        )

    lines.append("")

    return "\n".join(lines)


def command_export_names(args) -> int:
    path = Path(args.file)
    output_path = Path(args.output)

    delimiter = "\t" if args.format == "tsv" else ","

    try:
        data = read_file(path)
        require_card(path, data)
        validate_output_path(output_path, [path])

        text = render_export_names_report(path, data, delimiter)
        output_path.write_text(text, encoding="utf-8")
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    print("MiniJV880 CardRAM names exported")
    print("================================")
    print("")
    print(f"Source file: {path}")
    print(f"Output file: {output_path}")
    print(f"Format:      {args.format}")
    print("Rows:        80")
    print("")
    print("Original file was not modified.")

    return 0


def parse_import_area(value: str) -> str:
    area = value.strip().lower()

    if area == "performance":
        return "performance"

    if area == "patch":
        return "patch"

    raise ValueError(f"unknown import area: {value}")


def parse_import_slot(value: str, line_number: int) -> int:
    try:
        slot = int(value.strip(), 10)
    except ValueError:
        raise ValueError(f"line {line_number}: invalid slot number: {value}")

    return slot


def read_name_import_tsv(path: Path) -> list[tuple[int, str, int, str]]:
    if not path.is_file():
        raise ValueError(f"not a regular file: {path}")

    lines = path.read_text(encoding="utf-8").splitlines()

    if not lines:
        raise ValueError("name import file is empty")

    header = lines[0].split("\t")

    required_columns = ["area", "slot", "name"]

    for column in required_columns:
        if column not in header:
            raise ValueError(f"name import file is missing required column: {column}")

    area_index = header.index("area")
    slot_index = header.index("slot")
    name_index = header.index("name")

    updates = []
    seen_slots = set()

    for line_number, line in enumerate(lines[1:], start=2):
        if line.strip() == "":
            continue

        columns = line.split("\t")

        if len(columns) <= max(area_index, slot_index, name_index):
            raise ValueError(f"line {line_number}: not enough columns")

        area = parse_import_area(columns[area_index])
        slot = parse_import_slot(columns[slot_index], line_number)
        name = columns[name_index]

        key = (area, slot)

        if key in seen_slots:
            raise ValueError(f"line {line_number}: duplicate {area} slot {slot}")

        seen_slots.add(key)
        updates.append((line_number, area, slot, name))

    if not updates:
        raise ValueError("name import file contains no updates")

    return updates


def command_import_names(args) -> int:
    source_path = Path(args.source)
    names_path = Path(args.names_tsv)
    output_path = Path(args.output)

    try:
        source = read_file(source_path)
        require_card(source_path, source)
        validate_output_path(output_path, [source_path, names_path])

        updates = read_name_import_tsv(names_path)

        output = bytes(source)
        changed_count = 0
        shown_count = 0

        lines = [
            "MiniJV880 CardRAM names imported",
            "================================",
            "",
            f"Source file: {source_path}",
            f"Names file:  {names_path}",
            f"Output file: {output_path}",
            "",
            f"Source digest: 0x{fnv1a32(source):08X}",
            "",
            "Updates:",
            "  Line  Area         Slot  Offset   Old name      New name",
            "  ----  -----------  ----  -------  ------------  ------------",
        ]

        for line_number, area, slot, name in updates:
            if area == "performance":
                output, offset, old_name_data, new_name_data = set_performance_name(
                    output,
                    slot,
                    name,
                )
            elif area == "patch":
                output, offset, old_name_data, new_name_data = set_patch_name(
                    output,
                    slot,
                    name,
                )
            else:
                raise ValueError(f"line {line_number}: unknown area: {area}")

            old_name = decode_patch_name(old_name_data)
            new_name = decode_patch_name(new_name_data)

            if old_name == "":
                old_name = "<blank>"

            if new_name == "":
                new_name = "<blank>"

            changed = old_name_data != new_name_data

            if changed:
                changed_count += 1

            if not args.changed_only or changed:
                lines.append(
                    f"  {line_number:04d}  "
                    f"{area.capitalize():<11}  "
                    f"{slot:04d}  "
                    f"0x{offset:04X}   "
                    f"{old_name:<12.12}  "
                    f"{new_name:<12.12}"
                )
                shown_count += 1

        if args.changed_only and shown_count == 0:
            lines.append("  No name changes found.")

        output_path.write_bytes(output)

    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    lines.extend([
        "",
        f"Rows processed: {len(updates)}",
        f"Rows shown:     {shown_count}",
        f"Names changed:  {changed_count}",
        f"Output digest:  0x{fnv1a32(output):08X}",
        "",
        "Original file was not modified.",
        "",
    ])

    text = "\n".join(lines)
    write_or_print(text, args.report)

    return 0


def append_slot_summary(
    lines: list[str],
    title: str,
    data: bytes,
    area_start: int,
    slot_size: int,
    name_bytes: int,
    slot_count: int,
) -> None:
    lines.extend([
        title,
        "  #    Offset   Name          Slot digest",
        "  ---  -------  ------------  -----------",
    ])

    for slot in range(slot_count):
        offset = area_start + slot * slot_size
        slot_data = data[offset:offset + slot_size]

        if len(slot_data) != slot_size:
            break

        name = decode_patch_name(slot_data[:name_bytes])
        if name == "":
            name = "<blank>"

        lines.append(
            f"  {slot + 1:03d}  0x{offset:04X}   {name:<12.12}  0x{fnv1a32(slot_data):08X}"
        )

    lines.append("")


def render_summary_report(path: Path, data: bytes) -> str:
    performance_area_size = PERFORMANCE_SLOT_COUNT * PERFORMANCE_SLOT_SIZE
    patch_area_size = PATCH_SLOT_COUNT * PATCH_SLOT_SIZE

    performance_data = data[
        PERFORMANCE_AREA_START:PERFORMANCE_AREA_START + performance_area_size
    ]
    patch_data = data[
        PATCH_AREA_START:PATCH_AREA_START + patch_area_size
    ]
    rhythm_data = data[
        RHYTHM_AREA_START:RHYTHM_AREA_START + RHYTHM_AREA_SIZE
    ]

    lines = [
        "MiniJV880 CardRAM summary",
        "=========================",
        "",
        f"File:   {path.name}",
        f"Path:   {path.resolve()}",
        f"Size:   {len(data)} bytes",
        f"Digest: 0x{fnv1a32(data):08X}",
        "",
        "Areas:",
        f"  Header:      0x0000 - 0x001F   32 bytes",
        f"  Performance: 0x{PERFORMANCE_AREA_START:04X} - 0x{PERFORMANCE_AREA_START + performance_area_size - 1:04X}   {performance_area_size} bytes   digest 0x{fnv1a32(performance_data):08X}",
        f"  Patch:       0x{PATCH_AREA_START:04X} - 0x{PATCH_AREA_START + patch_area_size - 1:04X}   {patch_area_size} bytes   digest 0x{fnv1a32(patch_data):08X}",
        f"  Rhythm:      0x{RHYTHM_AREA_START:04X} - 0x{RHYTHM_AREA_START + RHYTHM_AREA_SIZE - 1:04X}   {RHYTHM_AREA_SIZE} bytes   digest 0x{fnv1a32(rhythm_data):08X}",
        "",
    ]

    append_slot_summary(
        lines,
        "Performance slots:",
        data,
        PERFORMANCE_AREA_START,
        PERFORMANCE_SLOT_SIZE,
        PERFORMANCE_NAME_BYTES,
        PERFORMANCE_SLOT_COUNT,
    )

    append_slot_summary(
        lines,
        "Patch slots:",
        data,
        PATCH_AREA_START,
        PATCH_SLOT_SIZE,
        PATCH_NAME_BYTES,
        PATCH_SLOT_COUNT,
    )

    return "\n".join(lines)


def command_summary(args) -> int:
    path = Path(args.file)

    try:
        data = read_file(path)
        require_card(path, data)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    text = render_summary_report(path, data)
    write_or_print(text, args.report)

    return 0


def render_rhythm_info_report(path: Path, data: bytes) -> str:

    rhythm_data = data[RHYTHM_AREA_START:RHYTHM_AREA_START + RHYTHM_AREA_SIZE]

    lines = [
        "MiniJV880 CardRAM rhythm area info",
        "==================================",
        "",
        f"File:   {path.name}",
        f"Path:   {path.resolve()}",
        f"Size:   {len(data)} bytes",
        f"Digest: 0x{fnv1a32(data):08X}",
        "",
        "Rhythm area:",
        f"  start:  0x{RHYTHM_AREA_START:04X}",
        f"  size:   {RHYTHM_AREA_SIZE} bytes",
        f"  end:    0x{RHYTHM_AREA_START + RHYTHM_AREA_SIZE - 1:04X}",
        f"  digest: 0x{fnv1a32(rhythm_data):08X}",
        "",
        "Note:",
        "  Rhythm area structure is not decoded yet.",
        "",
    ]

    return "\n".join(lines)


def command_rhythm_info(args) -> int:
    path = Path(args.file)

    try:
        data = read_file(path)
        require_card(path, data)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    text = render_rhythm_info_report(path, data)
    write_or_print(text, args.report)

    return 0


def render_compare_rhythm_report(
    left_path: Path,
    right_path: Path,
    left: bytes,
    right: bytes,
    max_ranges: int,
) -> tuple[str, bool]:
    left_rhythm_data = left[
        RHYTHM_AREA_START:
        RHYTHM_AREA_START + RHYTHM_AREA_SIZE
    ]
    right_rhythm_data = right[
        RHYTHM_AREA_START:
        RHYTHM_AREA_START + RHYTHM_AREA_SIZE
    ]

    diff_bytes = count_diff_bytes(left_rhythm_data, right_rhythm_data)
    ranges = find_diff_ranges(left_rhythm_data, right_rhythm_data)
    equal = diff_bytes == 0

    lines = [
        "MiniJV880 CardRAM rhythm compare",
        "================================",
        "",
        f"Left:   {left_path.name}",
        f"Path:   {left_path.resolve()}",
        f"Digest: 0x{fnv1a32(left):08X}",
        f"Rhythm digest: 0x{fnv1a32(left_rhythm_data):08X}",
        "",
        f"Right:  {right_path.name}",
        f"Path:   {right_path.resolve()}",
        f"Digest: 0x{fnv1a32(right):08X}",
        f"Rhythm digest: 0x{fnv1a32(right_rhythm_data):08X}",
        "",
        "Rhythm area:",
        f"  start: 0x{RHYTHM_AREA_START:04X}",
        f"  size:  {RHYTHM_AREA_SIZE} bytes",
        f"  end:   0x{RHYTHM_AREA_START + RHYTHM_AREA_SIZE - 1:04X}",
        "",
        f"Rhythm equal:      {'yes' if equal else 'no'}",
        f"Different bytes:   {diff_bytes}",
        f"Different ranges:  {len(ranges)}",
        "",
    ]

    if ranges:
        lines.append("Ranges:")

        shown = ranges if max_ranges == 0 else ranges[:max_ranges]

        for start, end in shown:
            lines.append(format_range(
                RHYTHM_AREA_START + start,
                RHYTHM_AREA_START + end,
            ))

        if max_ranges != 0 and len(ranges) > max_ranges:
            lines.append("")
            lines.append(f"... truncated after {max_ranges} ranges")

        lines.append("")

    return "\n".join(lines), equal


def command_compare_rhythm(args) -> int:
    left_path = Path(args.left)
    right_path = Path(args.right)

    try:
        left = read_file(left_path)
        right = read_file(right_path)
        require_card(left_path, left)
        require_card(right_path, right)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    text, equal = render_compare_rhythm_report(
        left_path,
        right_path,
        left,
        right,
        args.max_ranges,
    )

    write_or_print(text, args.report)

    return 0 if equal else 1


def render_rhythm_diff_report(
    left_path: Path,
    right_path: Path,
    left: bytes,
    right: bytes,
    max_ranges: int,
    preview_bytes: int,
) -> tuple[str, bool]:
    compare_text, equal = render_compare_rhythm_report(
        left_path,
        right_path,
        left,
        right,
        max_ranges,
    )

    left_rhythm_data = left[
        RHYTHM_AREA_START:
        RHYTHM_AREA_START + RHYTHM_AREA_SIZE
    ]
    right_rhythm_data = right[
        RHYTHM_AREA_START:
        RHYTHM_AREA_START + RHYTHM_AREA_SIZE
    ]

    ranges = find_diff_ranges(left_rhythm_data, right_rhythm_data)

    lines = [
        compare_text.rstrip(),
        "",
        "Rhythm diff preview",
        "-------------------",
        "",
    ]

    if not ranges:
        lines.append("No Rhythm differences.")
        lines.append("")
        return "\n".join(lines), True

    shown = ranges if max_ranges == 0 else ranges[:max_ranges]

    for index, (start, end) in enumerate(shown, start=1):
        length = min(preview_bytes, end - start + 1)
        abs_start = RHYTHM_AREA_START + start
        abs_end = RHYTHM_AREA_START + end

        lines.append(f"Range {index}: 0x{abs_start:04X} - 0x{abs_end:04X}")
        lines.append(f"Left hex:   {hex_bytes(left_rhythm_data, start, length)}")
        lines.append(f"Right hex:  {hex_bytes(right_rhythm_data, start, length)}")
        lines.append(f"Left text:  {ascii_preview(left_rhythm_data, start, length)}")
        lines.append(f"Right text: {ascii_preview(right_rhythm_data, start, length)}")

        if (end - start + 1) > preview_bytes:
            lines.append(f"... preview truncated to {preview_bytes} bytes")

        lines.append("")

    if max_ranges != 0 and len(ranges) > max_ranges:
        lines.append(f"Rhythm diff preview truncated after {max_ranges} ranges.")
        lines.append("")

    return "\n".join(lines), False


def command_rhythm_diff(args) -> int:
    left_path = Path(args.left)
    right_path = Path(args.right)

    try:
        left = read_file(left_path)
        right = read_file(right_path)
        require_card(left_path, left)
        require_card(right_path, right)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    text, equal = render_rhythm_diff_report(
        left_path,
        right_path,
        left,
        right,
        args.max_ranges,
        args.preview_bytes,
    )

    write_or_print(text, args.report)

    return 0 if equal else 1


def command_list_performances(args) -> int:
    path = Path(args.file)

    try:
        data = read_file(path)
        require_card(path, data)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    text = render_list_performances_report(path, data, args.slots)
    write_or_print(text, args.report)

    return 0


def command_list_patches(args) -> int:
    path = Path(args.file)

    try:
        data = read_file(path)
        require_card(path, data)
    except Exception as e:
        print(f"ERROR: {e}", file=sys.stderr)
        return 2

    text = render_list_patches_report(path, data, args.slots)
    write_or_print(text, args.report)

    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="MiniJV880 CardRAM PC-side inspection tool."
    )

    subparsers = parser.add_subparsers(dest="command", required=True)

    p_info = subparsers.add_parser(
        "info",
        help="show size, validity and MiniJV880 digest for one file",
    )
    p_info.add_argument("file")
    p_info.add_argument("--report", help="write report to a text file")
    p_info.set_defaults(func=command_info)

    p_compare = subparsers.add_parser(
        "compare",
        help="compare two 32768-byte CardRAM .bin files",
    )
    p_compare.add_argument("left")
    p_compare.add_argument("right")
    p_compare.add_argument(
        "--max-ranges",
        type=int,
        default=64,
        help="maximum number of ranges to show; use 0 for all",
    )
    p_compare.add_argument("--report", help="write report to a text file")
    p_compare.set_defaults(func=command_compare)

    p_diff = subparsers.add_parser(
        "diff",
        help="compare two CardRAM files and show hex/text previews of differences",
    )
    p_diff.add_argument("left")
    p_diff.add_argument("right")
    p_diff.add_argument(
        "--max-ranges",
        type=int,
        default=64,
        help="maximum number of ranges to show; use 0 for all",
    )
    p_diff.add_argument(
        "--preview-bytes",
        type=int,
        default=32,
        help="bytes to preview for each different range",
    )
    p_diff.add_argument("--report", help="write report to a text file")
    p_diff.set_defaults(func=command_diff)

    p_scan = subparsers.add_parser(
        "scan-names",
        help="scan the whole CardRAM file for printable ASCII runs",
    )
    p_scan.add_argument("file")
    p_scan.add_argument(
        "--min-length",
        type=int,
        default=4,
        help="minimum printable ASCII run length to report; default: 4",
    )
    p_scan.add_argument(
        "--max-results",
        type=int,
        default=200,
        help="maximum number of results to show; use 0 for all",
    )
    p_scan.add_argument("--report", help="write report to a text file")
    p_scan.set_defaults(func=command_scan_names)

    p_probe = subparsers.add_parser(
        "probe-stride",
        help="probe repeated name-like entries using custom start/stride parameters",
    )
    p_probe.add_argument("file")
    p_probe.add_argument(
        "--start",
        type=lambda value: int(value, 0),
        required=True,
        help="first offset to probe, decimal or hex, for example 0x0E49",
    )
    p_probe.add_argument(
        "--stride",
        type=lambda value: int(value, 0),
        required=True,
        help="distance between entries, decimal or hex, for example 0x016A",
    )
    p_probe.add_argument(
        "--count",
        type=int,
        default=64,
        help="number of entries to probe; default: 64",
    )
    p_probe.add_argument(
        "--name-bytes",
        type=int,
        default=12,
        help="number of bytes to decode as name at each offset; default: 12",
    )
    p_probe.add_argument("--report", help="write report to a text file")
    p_probe.set_defaults(func=command_probe_stride)

    p_rhythm = subparsers.add_parser(
        "rhythm-info",
        help="show raw Rhythm area offset, size and digest",
    )
    p_rhythm.add_argument("file")
    p_rhythm.add_argument("--report", help="write report to a text file")
    p_rhythm.set_defaults(func=command_rhythm_info)

    p_compare_rhythm = subparsers.add_parser(
        "compare-rhythm",
        help="compare only the raw Rhythm area between two CardRAM files",
    )
    p_compare_rhythm.add_argument("left")
    p_compare_rhythm.add_argument("right")
    p_compare_rhythm.add_argument(
        "--max-ranges",
        type=int,
        default=64,
        help="maximum number of ranges to show; use 0 for all",
    )
    p_compare_rhythm.add_argument("--report", help="write report to a text file")
    p_compare_rhythm.set_defaults(func=command_compare_rhythm)

    p_rhythm_diff = subparsers.add_parser(
        "rhythm-diff",
        help="show hex/ascii preview of raw Rhythm area differences",
    )
    p_rhythm_diff.add_argument("left")
    p_rhythm_diff.add_argument("right")
    p_rhythm_diff.add_argument(
        "--max-ranges",
        type=int,
        default=64,
        help="maximum number of ranges to show; use 0 for all",
    )
    p_rhythm_diff.add_argument(
        "--preview-bytes",
        type=int,
        default=32,
        help="maximum bytes to preview per range",
    )
    p_rhythm_diff.add_argument("--report", help="write report to a text file")
    p_rhythm_diff.set_defaults(func=command_rhythm_diff)

    p_summary = subparsers.add_parser(
        "summary",
        help="show Performance, Patch and Rhythm area digests plus slot names",
    )
    p_summary.add_argument("file")
    p_summary.add_argument("--report", help="write report to a text file")
    p_summary.set_defaults(func=command_summary)

    p_find_name = subparsers.add_parser(
        "find-name",
        help="search Performance and Patch names",
    )
    p_find_name.add_argument("file")
    p_find_name.add_argument("query")
    p_find_name.add_argument(
        "--exact",
        action="store_true",
        help="require an exact case-insensitive name match",
    )
    p_find_name.add_argument("--report", help="write report to a text file")
    p_find_name.set_defaults(func=command_find_name)

    p_compare_names = subparsers.add_parser(
        "compare-names",
        help="compare only Performance and Patch names between two CardRAM files",
    )
    p_compare_names.add_argument("left")
    p_compare_names.add_argument("right")
    p_compare_names.add_argument("--report", help="write report to a text file")
    p_compare_names.set_defaults(func=command_compare_names)

    p_check = subparsers.add_parser(
        "check-card",
        help="run read-only structural checks on one CardRAM file",
    )
    p_check.add_argument("file")
    p_check.add_argument("--report", help="write report to a text file")
    p_check.set_defaults(func=command_check_card)

    p_slot_info = subparsers.add_parser(
        "slot-info",
        help="show detailed information for one Performance or Patch slot",
    )
    p_slot_info.add_argument("file")
    p_slot_info.add_argument(
        "--area",
        choices=("performance", "patch"),
        required=True,
        help="slot area to inspect",
    )
    p_slot_info.add_argument(
        "--slot",
        type=int,
        required=True,
        help="slot number to inspect",
    )
    p_slot_info.add_argument(
        "--preview-bytes",
        type=int,
        default=64,
        help="number of bytes to show in hex/text preview; default: 64",
    )
    p_slot_info.add_argument("--report", help="write report to a text file")
    p_slot_info.set_defaults(func=command_slot_info)

    p_export_names = subparsers.add_parser(
        "export-names",
        help="export Performance and Patch names to TSV or CSV",
    )
    p_export_names.add_argument("file")
    p_export_names.add_argument(
        "--output",
        required=True,
        help="output text file; must not already exist",
    )
    p_export_names.add_argument(
        "--format",
        choices=("tsv", "csv"),
        default="tsv",
        help="export format; default: tsv",
    )
    p_export_names.set_defaults(func=command_export_names)

    p_import_names = subparsers.add_parser(
        "import-names",
        help="import edited Performance and Patch names from a TSV file and write a new card",
    )
    p_import_names.add_argument("source")
    p_import_names.add_argument("names_tsv")
    p_import_names.add_argument(
        "--output",
        required=True,
        help="new output .bin file; must not already exist",
    )
    p_import_names.add_argument("--report", help="write import report to a text file")
    p_import_names.add_argument(
        "--changed-only",
        action="store_true",
        help="show only rows whose name actually changes in the import report",
    )
    p_import_names.set_defaults(func=command_import_names)

    p_perf = subparsers.add_parser(
        "list-performances",
        help="list the 16 Data Card performance entries",
    )
    p_perf.add_argument("file")
    p_perf.add_argument(
        "--slots",
        type=int,
        default=PERFORMANCE_SLOT_COUNT,
        help=f"number of performance slots to list; default: {PERFORMANCE_SLOT_COUNT}",
    )
    p_perf.add_argument("--report", help="write report to a text file")
    p_perf.set_defaults(func=command_list_performances)

    p_list = subparsers.add_parser(
        "list-patches",
        help="list the 64 Data Card patch entries",
    )
    p_list.add_argument("file")
    p_list.add_argument(
        "--slots",
        type=int,
        default=PATCH_SLOT_COUNT,
        help=f"number of patch slots to list; default: {PATCH_SLOT_COUNT}",
    )
    p_list.add_argument("--report", help="write report to a text file")
    p_list.set_defaults(func=command_list_patches)

    p_compare_perf = subparsers.add_parser(
        "compare-performances",
        help="compare the 16 Data Card performance entries",
    )
    p_compare_perf.add_argument("left")
    p_compare_perf.add_argument("right")
    p_compare_perf.add_argument(
        "--slots",
        type=int,
        default=PERFORMANCE_SLOT_COUNT,
        help=f"number of performance slots to compare; default: {PERFORMANCE_SLOT_COUNT}",
    )
    p_compare_perf.add_argument("--report", help="write report to a text file")
    p_compare_perf.set_defaults(func=command_compare_performances)

    p_compare_patches = subparsers.add_parser(
        "compare-patches",
        help="compare the 64 Data Card patch entries",
    )
    p_compare_patches.add_argument("left")
    p_compare_patches.add_argument("right")
    p_compare_patches.add_argument(
        "--slots",
        type=int,
        default=PATCH_SLOT_COUNT,
        help=f"number of patch slots to compare; default: {PATCH_SLOT_COUNT}",
    )
    p_compare_patches.add_argument("--report", help="write report to a text file")
    p_compare_patches.set_defaults(func=command_compare_patches)

    p_swap_performances = subparsers.add_parser(
        "swap-performances",
        help="swap two complete Performance slots and write a new output card",
    )
    p_swap_performances.add_argument("source")
    p_swap_performances.add_argument(
        "--slot-a",
        type=int,
        required=True,
        help=f"first performance slot number, 1..{PERFORMANCE_SLOT_COUNT}",
    )
    p_swap_performances.add_argument(
        "--slot-b",
        type=int,
        required=True,
        help=f"second performance slot number, 1..{PERFORMANCE_SLOT_COUNT}",
    )
    p_swap_performances.add_argument(
        "--output",
        required=True,
        help="new output .bin file; must not already exist",
    )
    p_swap_performances.add_argument("--report", help="write operation report to a text file")
    p_swap_performances.set_defaults(func=command_swap_performances)

    p_swap_patches = subparsers.add_parser(
        "swap-patches",
        help="swap two complete Patch slots and write a new output card",
    )
    p_swap_patches.add_argument("source")
    p_swap_patches.add_argument(
        "--slot-a",
        type=int,
        required=True,
        help=f"first patch slot number, 1..{PATCH_SLOT_COUNT}",
    )
    p_swap_patches.add_argument(
        "--slot-b",
        type=int,
        required=True,
        help=f"second patch slot number, 1..{PATCH_SLOT_COUNT}",
    )
    p_swap_patches.add_argument(
        "--output",
        required=True,
        help="new output .bin file; must not already exist",
    )
    p_swap_patches.add_argument("--report", help="write operation report to a text file")
    p_swap_patches.set_defaults(func=command_swap_patches)

    p_set_performance_name = subparsers.add_parser(
        "set-performance-name",
        help="change the 12-byte name field of one Performance slot and write a new output card",
    )
    p_set_performance_name.add_argument("source")
    p_set_performance_name.add_argument(
        "--slot",
        type=int,
        required=True,
        help=f"performance slot number, 1..{PERFORMANCE_SLOT_COUNT}",
    )
    p_set_performance_name.add_argument(
        "--name",
        required=True,
        help=f"new performance name, printable ASCII, maximum {PERFORMANCE_NAME_BYTES} characters",
    )
    p_set_performance_name.add_argument(
        "--output",
        required=True,
        help="new output .bin file; must not already exist",
    )
    p_set_performance_name.add_argument("--report", help="write operation report to a text file")
    p_set_performance_name.set_defaults(func=command_set_performance_name)

    p_set_patch_name = subparsers.add_parser(
        "set-patch-name",
        help="change the 12-byte name field of one Patch slot and write a new output card",
    )
    p_set_patch_name.add_argument("source")
    p_set_patch_name.add_argument(
        "--slot",
        type=int,
        required=True,
        help=f"patch slot number, 1..{PATCH_SLOT_COUNT}",
    )
    p_set_patch_name.add_argument(
        "--name",
        required=True,
        help=f"new patch name, printable ASCII, maximum {PATCH_NAME_BYTES} characters",
    )
    p_set_patch_name.add_argument(
        "--output",
        required=True,
        help="new output .bin file; must not already exist",
    )
    p_set_patch_name.add_argument("--report", help="write operation report to a text file")
    p_set_patch_name.set_defaults(func=command_set_patch_name)

    p_clear_performance = subparsers.add_parser(
        "clear-performance",
        help="replace one Performance slot with the card's INITIAL DATA Performance template",
    )
    p_clear_performance.add_argument("source")
    p_clear_performance.add_argument(
        "--slot",
        type=int,
        required=True,
        help=f"performance slot number, 1..{PERFORMANCE_SLOT_COUNT}",
    )
    p_clear_performance.add_argument(
        "--output",
        required=True,
        help="new output .bin file; must not already exist",
    )
    p_clear_performance.add_argument("--report", help="write operation report to a text file")
    p_clear_performance.set_defaults(func=command_clear_performance)

    p_clear_patch = subparsers.add_parser(
        "clear-patch",
        help="replace one Patch slot with the card's INITIAL DATA Patch template",
    )
    p_clear_patch.add_argument("source")
    p_clear_patch.add_argument(
        "--slot",
        type=int,
        required=True,
        help=f"patch slot number, 1..{PATCH_SLOT_COUNT}",
    )
    p_clear_patch.add_argument(
        "--output",
        required=True,
        help="new output .bin file; must not already exist",
    )
    p_clear_patch.add_argument("--report", help="write operation report to a text file")
    p_clear_patch.set_defaults(func=command_clear_patch)

    p_move_performance = subparsers.add_parser(
        "move-performance-to-empty",
        help="move one Performance slot to an INITIAL DATA target slot and clear the source slot",
    )
    p_move_performance.add_argument("source")
    p_move_performance.add_argument(
        "--source-slot",
        type=int,
        required=True,
        help=f"source performance slot number, 1..{PERFORMANCE_SLOT_COUNT}",
    )
    p_move_performance.add_argument(
        "--target-slot",
        type=int,
        required=True,
        help=f"target performance slot number, 1..{PERFORMANCE_SLOT_COUNT}; must be INITIAL DATA",
    )
    p_move_performance.add_argument(
        "--output",
        required=True,
        help="new output .bin file; must not already exist",
    )
    p_move_performance.add_argument("--report", help="write operation report to a text file")
    p_move_performance.set_defaults(func=command_move_performance_to_empty)

    p_move_patch = subparsers.add_parser(
        "move-patch-to-empty",
        help="move one Patch slot to an INITIAL DATA target slot and clear the source slot",
    )
    p_move_patch.add_argument("source")
    p_move_patch.add_argument(
        "--source-slot",
        type=int,
        required=True,
        help=f"source patch slot number, 1..{PATCH_SLOT_COUNT}",
    )
    p_move_patch.add_argument(
        "--target-slot",
        type=int,
        required=True,
        help=f"target patch slot number, 1..{PATCH_SLOT_COUNT}; must be INITIAL DATA",
    )
    p_move_patch.add_argument(
        "--output",
        required=True,
        help="new output .bin file; must not already exist",
    )
    p_move_patch.add_argument("--report", help="write operation report to a text file")
    p_move_patch.set_defaults(func=command_move_patch_to_empty)

    p_copy_perf = subparsers.add_parser(
        "copy-performance",
        help="copy one performance slot from source card to destination card and write a new output card",
    )
    p_copy_perf.add_argument("source")
    p_copy_perf.add_argument("dest")
    p_copy_perf.add_argument(
        "--source-slot",
        type=int,
        required=True,
        help=f"source performance slot number, 1..{PERFORMANCE_SLOT_COUNT}",
    )
    p_copy_perf.add_argument(
        "--target-slot",
        type=int,
        required=True,
        help=f"target performance slot number, 1..{PERFORMANCE_SLOT_COUNT}",
    )
    p_copy_perf.add_argument(
        "--output",
        required=True,
        help="new output .bin file; must not already exist",
    )
    p_copy_perf.add_argument("--report", help="write operation report to a text file")
    p_copy_perf.set_defaults(func=command_copy_performance)

    p_copy_perf_bank = subparsers.add_parser(
        "copy-performance-bank",
        help="copy the full 16-slot Performance bank from source card to destination card and write a new output card",
    )
    p_copy_perf_bank.add_argument("source")
    p_copy_perf_bank.add_argument("dest")
    p_copy_perf_bank.add_argument(
        "--output",
        required=True,
        help="new output .bin file; must not already exist",
    )
    p_copy_perf_bank.add_argument("--report", help="write operation report to a text file")
    p_copy_perf_bank.set_defaults(func=command_copy_performance_bank)

    p_copy_patch = subparsers.add_parser(
        "copy-patch",
        help="copy one patch slot from source card to destination card and write a new output card",
    )
    p_copy_patch.add_argument("source")
    p_copy_patch.add_argument("dest")
    p_copy_patch.add_argument(
        "--source-slot",
        type=int,
        required=True,
        help=f"source patch slot number, 1..{PATCH_SLOT_COUNT}",
    )
    p_copy_patch.add_argument(
        "--target-slot",
        type=int,
        required=True,
        help=f"target patch slot number, 1..{PATCH_SLOT_COUNT}",
    )
    p_copy_patch.add_argument(
        "--output",
        required=True,
        help="new output .bin file; must not already exist",
    )
    p_copy_patch.add_argument("--report", help="write operation report to a text file")
    p_copy_patch.set_defaults(func=command_copy_patch)

    p_copy_patch_bank = subparsers.add_parser(
        "copy-patch-bank",
        help="copy the full 64-slot Patch bank from source card to destination card and write a new output card",
    )
    p_copy_patch_bank.add_argument("source")
    p_copy_patch_bank.add_argument("dest")
    p_copy_patch_bank.add_argument(
        "--output",
        required=True,
        help="new output .bin file; must not already exist",
    )
    p_copy_patch_bank.add_argument("--report", help="write operation report to a text file")
    p_copy_patch_bank.set_defaults(func=command_copy_patch_bank)

    p_copy_rhythm = subparsers.add_parser(
        "copy-rhythm",
        help="copy the raw Rhythm area from source card to destination card and write a new output card",
    )
    p_copy_rhythm.add_argument("source")
    p_copy_rhythm.add_argument("dest")
    p_copy_rhythm.add_argument(
        "--output",
        required=True,
        help="new output .bin file; must not already exist",
    )
    p_copy_rhythm.add_argument("--report", help="write operation report to a text file")
    p_copy_rhythm.set_defaults(func=command_copy_rhythm)

    p_compose = subparsers.add_parser(
        "compose-card",
        help="compose a new card from a base card and optional Performance, Patch and Rhythm sources",
    )
    p_compose.add_argument("base")
    p_compose.add_argument(
        "--performance-source",
        help="card file providing the full 16-slot Performance bank",
    )
    p_compose.add_argument(
        "--patch-source",
        help="card file providing the full 64-slot Patch bank",
    )
    p_compose.add_argument(
        "--rhythm-source",
        help="card file providing the raw Rhythm area",
    )
    p_compose.add_argument(
        "--output",
        required=True,
        help="new output .bin file; must not already exist",
    )
    p_compose.add_argument("--report", help="write operation report to a text file")
    p_compose.set_defaults(func=command_compose_card)

    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    if hasattr(args, "max_ranges") and args.max_ranges < 0:
        print("ERROR: --max-ranges cannot be negative", file=sys.stderr)
        return 2

    if hasattr(args, "preview_bytes") and args.preview_bytes <= 0:
        print("ERROR: --preview-bytes must be greater than zero", file=sys.stderr)
        return 2

    if hasattr(args, "slots") and args.slots <= 0:
        print("ERROR: --slots must be greater than zero", file=sys.stderr)
        return 2

    if hasattr(args, "min_length") and args.min_length <= 0:
        print("ERROR: --min-length must be greater than zero", file=sys.stderr)
        return 2

    if hasattr(args, "max_results") and args.max_results < 0:
        print("ERROR: --max-results cannot be negative", file=sys.stderr)
        return 2

    if hasattr(args, "start") and args.start < 0:
        print("ERROR: --start cannot be negative", file=sys.stderr)
        return 2

    if hasattr(args, "stride") and args.stride <= 0:
        print("ERROR: --stride must be greater than zero", file=sys.stderr)
        return 2

    if hasattr(args, "count") and args.count <= 0:
        print("ERROR: --count must be greater than zero", file=sys.stderr)
        return 2

    if hasattr(args, "name_bytes") and args.name_bytes <= 0:
        print("ERROR: --name-bytes must be greater than zero", file=sys.stderr)
        return 2

    if hasattr(args, "preview_bytes") and args.preview_bytes <= 0:
        print("ERROR: --preview-bytes must be greater than zero", file=sys.stderr)
        return 2

    if hasattr(args, "source_slot") or hasattr(args, "target_slot"):
        slot_limit = PATCH_SLOT_COUNT
        slot_label = "patch"

        if getattr(args, "command", "") == "copy-performance":
            slot_limit = PERFORMANCE_SLOT_COUNT
            slot_label = "performance"

        if hasattr(args, "source_slot") and (
            args.source_slot < 1 or args.source_slot > slot_limit
        ):
            print(f"ERROR: --source-slot must be between 1 and {slot_limit} for {slot_label}", file=sys.stderr)
            return 2

        if hasattr(args, "target_slot") and (
            args.target_slot < 1 or args.target_slot > slot_limit
        ):
            print(f"ERROR: --target-slot must be between 1 and {slot_limit} for {slot_label}", file=sys.stderr)
            return 2

    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
