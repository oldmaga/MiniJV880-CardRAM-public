#!/usr/bin/env bash
set -u

# MiniJV880 TFTP helper GUI
#
# Requirements:
#   - bash
#   - zenity
#   - python3
#   - curl
#   - tftp
#
# Optional host override:
#   MINIJV880_HOST=192.168.1.50 tools/minijv880_tftp_gui.sh

HOST_DEFAULT="${MINIJV880_HOST:-192.168.1.50}"
HTTP_PORT="${MINIJV880_HTTP_PORT:-8080}"

SCRIPT_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
TFTP_PUT="$SCRIPT_DIR/minijv880_tftp_put.py"

need_cmd() {
    if ! command -v "$1" >/dev/null 2>&1; then
        zenity --error \
            --title="MiniJV880 TFTP" \
            --text="Missing required command: $1"
        exit 1
    fi
}

need_executable() {
    if [ ! -x "$1" ]; then
        zenity --error \
            --title="MiniJV880 TFTP" \
            --text="Missing or non-executable helper:

$1

Please make sure minijv880_tftp_put.py is present and executable."
        exit 1
    fi
}

is_tftp_error() {
    printf '%s\n' "$1" | grep -qi '^Error code '
}

ask_host() {
    HOST="$(zenity \
        --entry \
        --title="MiniJV880 TFTP" \
        --text="MiniJV880 IP address:" \
        --entry-text="$HOST_DEFAULT")" || exit 0

    if [ -z "$HOST" ]; then
        zenity --error \
            --title="MiniJV880 TFTP" \
            --text="The IP address is empty."
        exit 1
    fi
}

validate_remote_folder() {
    local folder="$1"

    if [ -z "$folder" ]; then
        return 1
    fi

    case "$folder" in
        "."|".."|"Roland-PN")
            return 1
            ;;
        */*|*\\*)
            return 1
            ;;
    esac

    return 0
}

url_encode_segment() {
    python3 - "$1" <<'PY'
import sys
from urllib.parse import quote
print(quote(sys.argv[1], safe=""))
PY
}

html_escape_text() {
    python3 - "$1" <<'PY'
import html
import sys
print(html.escape(sys.argv[1], quote=True))
PY
}

fetch_remote_folder_listing() {
    local folder="$1"
    local encoded_folder url

    encoded_folder="$(url_encode_segment "$folder")" || return 2
    url="http://$HOST:$HTTP_PORT/browse/PN-JV80/$encoded_folder"

    curl --silent --show-error --fail --max-time 8 "$url"
}

remote_folder_listing_has_error() {
    printf '%s\n' "$1" | grep -Eqi 'Cannot open directory|404 Not Found|Not Found|Internal Server Error'
}

remote_file_listed() {
    local listing="$1"
    local filename="$2"
    local encoded_name escaped_name

    encoded_name="$(url_encode_segment "$filename")"
    escaped_name="$(html_escape_text "$filename")"

    printf '%s\n' "$listing" | grep -Fq "$filename" && return 0
    printf '%s\n' "$listing" | grep -Fq "$encoded_name" && return 0
    printf '%s\n' "$listing" | grep -Fq "$escaped_name" && return 0

    return 1
}

fetch_pnjv80_root_listing() {
    local url

    url="http://$HOST:$HTTP_PORT/browse/PN-JV80"

    curl --silent --show-error --fail --max-time 8 "$url"
}

extract_pnjv80_folders() {
    python3 -c '
import html
import sys
from html.parser import HTMLParser
from urllib.parse import unquote, urlparse

class LinkParser(HTMLParser):
    def __init__(self):
        super().__init__()
        self.folders = []
        self.seen = set()

    def add_folder(self, name):
        name = unquote(name).strip().strip("/")

        if name in ("", ".", "..", "Roland-PN"):
            return

        if "\\" in name or "/" in name:
            return

        if name not in self.seen:
            self.seen.add(name)
            self.folders.append(name)

    def handle_starttag(self, tag, attrs):
        if tag.lower() != "a":
            return

        href = ""
        for key, value in attrs:
            if key.lower() == "href" and value is not None:
                href = html.unescape(value).strip()
                break

        if not href:
            return

        parsed = urlparse(href)

        if parsed.scheme and parsed.scheme not in ("http", "https"):
            return

        path = unquote(parsed.path)

        prefixes = (
            "/browse/PN-JV80/",
            "browse/PN-JV80/",
        )

        for prefix in prefixes:
            if path.startswith(prefix):
                suffix = path[len(prefix):]
                suffix = suffix.split("?", 1)[0].split("#", 1)[0].strip("/")
                if suffix and "/" not in suffix:
                    self.add_folder(suffix)
                return

        if not parsed.scheme and not parsed.netloc:
            candidate = href.split("?", 1)[0].split("#", 1)[0].strip("/")
            if candidate and "/" not in candidate:
                self.add_folder(candidate)

parser = LinkParser()
parser.feed(sys.stdin.read())

for folder in sorted(parser.folders):
    print(folder)
'
}

fetch_cardram_remote_names() {
    python3 - "$HOST" "$HTTP_PORT" <<'PY'
import html
import sys
from html.parser import HTMLParser
from urllib.parse import parse_qs, unquote, urljoin, urlparse
from urllib.request import Request, urlopen


host = sys.argv[1]
port = sys.argv[2]

base_url = f"http://{host}:{port}/cardram-list"
expected_netloc = f"{host}:{port}"


def is_simple_bin_name(name):
    name = unquote(name).strip().strip("/")

    if name in ("", ".", ".."):
        return False

    if "/" in name or "\\" in name:
        return False

    return name.lower().endswith(".bin")


class CardRAMListParser(HTMLParser):
    def __init__(self, page_url):
        super().__init__()
        self.page_url = page_url
        self.names = []
        self.page_links = []
        self._active_href = None

    def add_name(self, name):
        name = unquote(name).strip().strip("/")

        if is_simple_bin_name(name) and name not in self.names:
            self.names.append(name)

    def add_page_link(self, href):
        url = urljoin(self.page_url, html.unescape(href))
        parsed = urlparse(url)

        if parsed.netloc != expected_netloc:
            return

        if parsed.path != "/cardram-list":
            return

        normalized = parsed.geturl()

        if normalized not in self.page_links:
            self.page_links.append(normalized)

    def handle_starttag(self, tag, attrs):
        if tag.lower() != "a":
            self._active_href = None
            return

        href = ""

        for key, value in attrs:
            if key.lower() == "href" and value is not None:
                href = html.unescape(value).strip()
                break

        self._active_href = href or None

        if not href:
            return

        url = urljoin(self.page_url, href)
        parsed = urlparse(url)

        if parsed.netloc != expected_netloc:
            return

        if parsed.path == "/cardram-list":
            self.add_page_link(href)

        query = parse_qs(parsed.query)

        for key in ("name", "file"):
            for value in query.get(key, []):
                self.add_name(value)

        candidate = unquote(parsed.path.rsplit("/", 1)[-1])
        self.add_name(candidate)

    def handle_data(self, data):
        text = html.unescape(data).strip()

        if text:
            self.add_name(text)


def fetch_page(url):
    request = Request(url, headers={"User-Agent": "MiniJV880 TFTP helper"})
    with urlopen(request, timeout=8) as response:
        charset = response.headers.get_content_charset() or "utf-8"
        return response.read().decode(charset, errors="replace")


seen_pages = set()
pending_pages = [base_url]
names = []

while pending_pages and len(seen_pages) < 50:
    page_url = pending_pages.pop(0)

    if page_url in seen_pages:
        continue

    seen_pages.add(page_url)

    text = fetch_page(page_url)

    parser = CardRAMListParser(page_url)
    parser.feed(text)

    for name in parser.names:
        if name not in names:
            names.append(name)

    for link in parser.page_links:
        if link not in seen_pages and link not in pending_pages:
            pending_pages.append(link)

for name in sorted(names):
    print(name)
PY
}

manual_remote_cardram_name_entry() {
    zenity \
        --entry \
        --width=620 \
        --title="MiniJV880 Data Card download" \
        --text="Remote Data Card file name inside SD:/CARD-RAM/

Use a single .bin file name, for example:

  INTERNAL.bin

Do not use slashes or backslashes." \
        --entry-text="INTERNAL.bin"
}

select_remote_cardram_name() {
    local remote_names tmp_http_error http_status http_error selected
    local -a cardram_names

    tmp_http_error="$(mktemp)"
    remote_names="$(fetch_cardram_remote_names 2>"$tmp_http_error")"
    http_status=$?
    http_error="$(cat "$tmp_http_error" 2>/dev/null || true)"
    rm -f "$tmp_http_error"

    if [ "$http_status" -ne 0 ] || [ -z "$remote_names" ]; then
        if zenity --question \
            --width=660 \
            --title="MiniJV880 Data Card download" \
            --text="The CARD-RAM file list could not be loaded from the MiniJV880 HTTP CardRAM list page.

HTTP URL checked:

  http://$HOST:$HTTP_PORT/cardram-list

Details:

${http_error:-No additional HTTP error text.}

Do you want to enter the remote Data Card file name manually?"; then

            manual_remote_cardram_name_entry
            return $?
        fi

        return 1
    fi

    mapfile -t cardram_names < <(printf '%s\n' "$remote_names")

    selected="$(zenity \
        --list \
        --width=560 \
        --height=460 \
        --title="MiniJV880 Data Card download" \
        --text="Select the Data Card .bin file to download from SD:/CARD-RAM/:" \
        --column="Data Card .bin" \
        "${cardram_names[@]}")" || return 1

    if [ -z "$selected" ]; then
        return 1
    fi

    printf '%s\n' "$selected"
}

manual_remote_folder_entry() {
    zenity \
        --entry \
        --title="MiniJV880 SYX batch upload" \
        --text="Remote PN-JV80 subfolder name already existing on the SD card:" \
        --entry-text="40-USER"
}

select_remote_folder() {
    local root_listing tmp_http_error http_status http_error selected
    local -a remote_folders

    tmp_http_error="$(mktemp)"
    root_listing="$(fetch_pnjv80_root_listing 2>"$tmp_http_error")"
    http_status=$?
    http_error="$(cat "$tmp_http_error" 2>/dev/null || true)"
    rm -f "$tmp_http_error"

    if [ "$http_status" -ne 0 ] || remote_folder_listing_has_error "$root_listing"; then
        if zenity --question \
            --width=620 \
            --title="MiniJV880 SYX batch upload" \
            --text="The PN-JV80 folder list could not be loaded from the MiniJV880 HTTP browse page.

HTTP URL checked:

  http://$HOST:$HTTP_PORT/browse/PN-JV80

Details:

${http_error:-No additional HTTP error text.}

Do you want to enter the destination folder manually?"; then

            manual_remote_folder_entry
            return $?
        fi

        return 1
    fi

    mapfile -t remote_folders < <(printf '%s\n' "$root_listing" | extract_pnjv80_folders)

    if [ "${#remote_folders[@]}" -eq 0 ]; then
        if zenity --question \
            --width=620 \
            --title="MiniJV880 SYX batch upload" \
            --text="No writable PN-JV80 subfolders were found in the MiniJV880 HTTP browse page.

The special read-only Roland-PN folder is not offered as an upload destination.

Do you want to enter the destination folder manually?"; then

            manual_remote_folder_entry
            return $?
        fi

        return 1
    fi

    selected="$(zenity \
        --list \
        --width=520 \
        --height=420 \
        --title="MiniJV880 SYX batch upload" \
        --text="Select the destination PN-JV80 subfolder:" \
        --column="Folder" \
        "${remote_folders[@]}")" || return 1

    if [ -z "$selected" ]; then
        return 1
    fi

    printf '%s\n' "$selected"
}

run_tftp_put_with_progress() {
    local local_file="$1"
    local remote_path="$2"
    local title="$3"
    local message="$4"
    local tmp_error tmp_status status

    TFTP_OUTPUT=""
    TFTP_STATUS=1

    tmp_error="$(mktemp)"
    tmp_status="$(mktemp)"

    (
        "$TFTP_PUT" \
            --zenity \
            "$HOST" \
            "$local_file" \
            "$remote_path" \
            2>"$tmp_error"

        status=$?
        echo "$status" > "$tmp_status"
    ) | zenity \
        --progress \
        --percentage=0 \
        --auto-close \
        --no-cancel \
        --width=560 \
        --title="$title" \
        --text="$message"

    TFTP_OUTPUT="$(cat "$tmp_error" 2>/dev/null || true)"

    if [ -s "$tmp_status" ]; then
        TFTP_STATUS="$(cat "$tmp_status")"
    else
        TFTP_STATUS=1
    fi

    rm -f "$tmp_error" "$tmp_status"

    if [ "$TFTP_STATUS" -eq 0 ]; then
        return 0
    fi

    return 1
}

run_tftp_get_with_progress() {
    local remote_path="$1"
    local local_output="$2"
    local title="$3"
    local message="$4"
    local tmp_error tmp_stdout tmp_status tmp_download status

    TFTP_OUTPUT=""
    TFTP_STATUS=1

    tmp_error="$(mktemp)"
    tmp_stdout="$(mktemp)"
    tmp_status="$(mktemp)"
    tmp_download="$(mktemp)"

    (
        tftp "$HOST" -m binary -c get "$remote_path" "$tmp_download" >"$tmp_stdout" 2>"$tmp_error"

        status=$?
        echo "$status" > "$tmp_status"
    ) | zenity \
        --progress \
        --pulsate \
        --auto-close \
        --no-cancel \
        --width=560 \
        --title="$title" \
        --text="$message"

    TFTP_OUTPUT="$(
        cat "$tmp_stdout" 2>/dev/null || true
        cat "$tmp_error" 2>/dev/null || true
    )"

    if [ -s "$tmp_status" ]; then
        TFTP_STATUS="$(cat "$tmp_status")"
    else
        TFTP_STATUS=1
    fi

    rm -f "$tmp_error" "$tmp_stdout" "$tmp_status"

    if [ "$TFTP_STATUS" -eq 0 ] && [ -f "$tmp_download" ]; then
        if mv -f "$tmp_download" "$local_output"; then
            return 0
        fi

        TFTP_OUTPUT="$TFTP_OUTPUT
Could not move downloaded temporary file to:

$local_output"

        rm -f "$tmp_download"
        return 1
    fi

    rm -f "$tmp_download"
    return 1
}

write_file_digest_report() {
    local file="$1"

    python3 - "$file" <<'PY'
import os
import sys

FNV_OFFSET = 2166136261
FNV_PRIME = 16777619

path = sys.argv[1]

try:
    size = os.path.getsize(path)

    digest = FNV_OFFSET

    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            for b in chunk:
                digest = ((digest ^ b) * FNV_PRIME) & 0xFFFFFFFF

    print("MiniJV880 file digest")
    print("=====================")
    print()
    print(f"File:   {os.path.basename(path)}")
    print(f"Path:   {os.path.abspath(path)}")
    print(f"Size:   {size} bytes")
    print()
    print("MiniJV880 digest:")
    print(f"0x{digest:08X}")

except OSError as e:
    print(f"ERROR: {e}", file=sys.stderr)
    sys.exit(1)
PY
}

get_file_digest_info() {
    local file="$1"

    python3 - "$file" <<'PY'
import os
import sys

FNV_OFFSET = 2166136261
FNV_PRIME = 16777619

path = sys.argv[1]

try:
    size = os.path.getsize(path)

    digest = FNV_OFFSET

    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            for b in chunk:
                digest = ((digest ^ b) * FNV_PRIME) & 0xFFFFFFFF

    print(f"{size} 0x{digest:08X}")

except OSError as e:
    print(f"ERROR: {e}", file=sys.stderr)
    sys.exit(1)
PY
}

is_cardram_bin_remote_name() {
    local name="$1"
    local lower_name

    if [ -z "$name" ]; then
        return 1
    fi

    case "$name" in
        "."|"..")
            return 1
            ;;
        */*|*\\*)
            return 1
            ;;
    esac

    lower_name="$(printf '%s' "$name" | tr '[:upper:]' '[:lower:]')"

    case "$lower_name" in
        *.bin)
            ;;
        *)
            return 1
            ;;
    esac

    return 0
}

show_file_digest() {
    local file tmp_log tmp_error status

    file="$(zenity \
        --file-selection \
        --title="Select file to calculate MiniJV880 digest")" || return 0

    if [ ! -f "$file" ]; then
        zenity --error \
            --title="MiniJV880 file digest" \
            --text="Selected path is not a regular file:

$file"
        return 1
    fi

    tmp_log="$(mktemp)"
    tmp_error="$(mktemp)"

    write_file_digest_report "$file" > "$tmp_log" 2>"$tmp_error"
    status=$?

    if [ "$status" -ne 0 ]; then
        zenity --error \
            --width=620 \
            --title="MiniJV880 file digest" \
            --text="Could not calculate file digest.

File:

$file

Details:

$(cat "$tmp_error" 2>/dev/null || true)"

        rm -f "$tmp_log" "$tmp_error"
        return 1
    fi

    zenity \
        --info \
        --ok-label="Back" \
        --no-wrap \
        --title="MiniJV880 file digest" \
        --width=620 \
        --text="$(cat "$tmp_log")"

    rm -f "$tmp_log" "$tmp_error"
}

upload_syx_batch() {
    local local_dir remote_folder tmp_log tmp_result total ok skipped failed
    local folder_listing tmp_http_error http_status http_error

    local_dir="$(zenity \
        --file-selection \
        --directory \
        --title="Select local folder containing .syx files")" || return 0

    remote_folder="$(select_remote_folder)" || return 0

    if ! validate_remote_folder "$remote_folder"; then
        zenity --error \
            --title="MiniJV880 SYX batch upload" \
            --text="Invalid remote folder name.

Use only one existing PN-JV80 subfolder name, for example:

  40-USER

Do not use:
  - Roland-PN
  - slashes
  - backslashes
  - empty names"
        return 1
    fi

    mapfile -d '' FILES < <(find "$local_dir" -maxdepth 1 -type f -iname '*.syx' -print0 | sort -z)

    total="${#FILES[@]}"

    if [ "$total" -eq 0 ]; then
        zenity --info \
            --title="MiniJV880 SYX batch upload" \
            --text="No .syx files found in:

$local_dir"
        return 0
    fi

    tmp_http_error="$(mktemp)"
    folder_listing="$(fetch_remote_folder_listing "$remote_folder" 2>"$tmp_http_error")"
    http_status=$?
    http_error="$(cat "$tmp_http_error" 2>/dev/null || true)"
    rm -f "$tmp_http_error"

    if [ "$http_status" -ne 0 ] || remote_folder_listing_has_error "$folder_listing"; then
        zenity --error \
            --width=620 \
            --title="MiniJV880 SYX batch upload" \
            --text="Destination folder check failed.

Remote folder:

  SD:/PN-JV80/$remote_folder/

The folder may not exist on the SD card, or the MiniJV880 HTTP browse page may be unavailable.

HTTP URL checked:

  http://$HOST:$HTTP_PORT/browse/PN-JV80/$remote_folder

Details:

${http_error:-No additional HTTP error text.}"
        return 1
    fi

    if ! zenity --question \
        --width=560 \
        --title="MiniJV880 SYX batch upload" \
        --text="Upload $total .syx file(s) to:

MiniJV880: $HOST
Remote folder: SD:/PN-JV80/$remote_folder/

Files already listed in the destination folder will be skipped.
No overwrite is performed.

Continue?"; then
        return 0
    fi

    tmp_log="$(mktemp)"
    tmp_result="$(mktemp)"

    (
        count=0
        ok=0
        skipped=0
        failed=0

        echo "0"
        echo "# Preparing upload..."

        for file in "${FILES[@]}"; do
            basename="$(basename "$file")"
            remote_path="$remote_folder/$basename"

            echo "# Checking $((count + 1))/$total: $basename"
            echo "[$((count + 1))/$total] Checking: $file -> $remote_path" >> "$tmp_log"

            if remote_file_listed "$folder_listing" "$basename"; then
                echo "# Skipping $((count + 1))/$total: $basename already exists"
                echo "SKIPPED: $basename - already exists in destination folder" >> "$tmp_log"
                skipped=$((skipped + 1))

                echo >> "$tmp_log"

                count=$((count + 1))
                echo "$((count * 100 / total))"

                sleep 0.1
                continue
            fi

            echo "# Uploading $((count + 1))/$total: $basename"
            echo "UPLOAD: $basename" >> "$tmp_log"

            tmp_error="$(mktemp)"

            "$TFTP_PUT" \
                --zenity \
                "$HOST" \
                "$file" \
                "$remote_path" \
                2>"$tmp_error" | while IFS= read -r line; do

                case "$line" in
                    \#*)
                        echo "# Uploading $((count + 1))/$total: $basename - ${line#\# }"
                        ;;
                    ''|*[!0-9]*)
                        :
                        ;;
                    *)
                        echo "$(( (count * 100 + line) / total ))"
                        ;;
                esac
            done

            status=${PIPESTATUS[0]}
            output="$(cat "$tmp_error" 2>/dev/null || true)"
            rm -f "$tmp_error"

            if [ -n "$output" ]; then
                printf '%s\n' "$output" >> "$tmp_log"
            fi

            if [ "$status" -eq 0 ]; then
                echo "OK: $basename" >> "$tmp_log"
                ok=$((ok + 1))
                folder_listing="$folder_listing
$basename"
            else
                if printf '%s\n' "$output" | grep -qi 'TFTP error 2: Access violation'; then
                    echo "FAILED: $basename - access violation after preflight" >> "$tmp_log"
                    echo "Reason: destination listing did not show the file, but the firmware rejected the write." >> "$tmp_log"
                    echo "Possible causes: stale HTTP listing, SD write problem, leftover .tmp file, or firmware-side path rejection." >> "$tmp_log"
                elif printf '%s\n' "$output" | grep -qi 'timeout'; then
                    echo "FAILED: $basename - timeout waiting for MiniJV880 TFTP response" >> "$tmp_log"
                else
                    echo "FAILED: $basename - TFTP upload failed" >> "$tmp_log"
                fi

                failed=$((failed + 1))
            fi

            echo >> "$tmp_log"

            count=$((count + 1))
            echo "$((count * 100 / total))"

            sleep 0.2
        done

        {
            echo "Summary"
            echo "-------"
            echo "Uploaded OK: $ok"
            echo "Skipped:     $skipped"
            echo "Failed:      $failed"
        } >> "$tmp_log"

        echo "$ok $skipped $failed" > "$tmp_result"

        echo "100"
        echo "# Finished."
    ) | zenity \
        --progress \
        --percentage=0 \
        --auto-close \
        --no-cancel \
        --width=560 \
        --title="MiniJV880 SYX batch upload" \
        --text="Uploading .syx files..."

    if [ -s "$tmp_result" ]; then
        read -r ok skipped failed < "$tmp_result"
    else
        ok=0
        skipped=0
        failed="$total"
        {
            echo
            echo "Summary unavailable."
            echo "The progress window may have been closed before completion."
        } >> "$tmp_log"
    fi

    zenity \
        --text-info \
        --title="MiniJV880 SYX batch upload result" \
        --width=820 \
        --height=560 \
        --filename="$tmp_log"

    rm -f "$tmp_log" "$tmp_result"
}

upload_cardram_bin() {
    local file remote_name remote_path tmp_log
    local tmp_digest_error digest_info digest_status digest_size digest_value
    local default_remote_name

    file="$(zenity \
        --file-selection \
        --title="Select Data Card .bin image to upload via TFTP")" || return 0

    if [ ! -f "$file" ]; then
        zenity --error \
            --title="MiniJV880 Data Card upload" \
            --text="Selected path is not a regular file:

$file"
        return 1
    fi

    tmp_digest_error="$(mktemp)"
    digest_info="$(get_file_digest_info "$file" 2>"$tmp_digest_error")"
    digest_status=$?

    if [ "$digest_status" -ne 0 ]; then
        zenity --error \
            --width=620 \
            --title="MiniJV880 Data Card upload" \
            --text="Could not calculate MiniJV880 digest.

File:

$file

Details:

$(cat "$tmp_digest_error" 2>/dev/null || true)"
        rm -f "$tmp_digest_error"
        return 1
    fi

    rm -f "$tmp_digest_error"

    digest_size="${digest_info%% *}"
    digest_value="${digest_info#* }"

    if [ "$digest_size" != "32768" ]; then
        zenity --error \
            --width=620 \
            --title="MiniJV880 Data Card upload" \
            --text="Invalid Data Card image size.

Selected file:

$file

Detected size:
$digest_size bytes

Required size:
32768 bytes

Only a single 32 KB Data Card .bin image can be uploaded."
        return 1
    fi

    default_remote_name="$(basename "$file")"

    remote_name="$(zenity \
        --entry \
        --width=620 \
        --title="MiniJV880 Data Card upload" \
        --text="Remote Data Card file name inside SD:/CARD-RAM/

Use a single .bin file name, for example:

  Strings.bin

Do not use slashes or backslashes." \
        --entry-text="$default_remote_name")" || return 0

    if ! is_cardram_bin_remote_name "$remote_name"; then
        zenity --error \
            --width=620 \
            --title="MiniJV880 Data Card upload" \
            --text="Invalid remote Data Card file name.

Use only one .bin file name, for example:

  Strings.bin

Do not use:
  - empty names
  - . or ..
  - slashes
  - backslashes"
        return 1
    fi

    remote_path="CARD-RAM/$remote_name"

    if ! zenity --question \
        --width=700 \
        --title="MiniJV880 Data Card upload" \
        --text="This will upload:

$file

File size:
$digest_size bytes

MiniJV880 digest:
$digest_value

using the remote TFTP name:

$remote_path

The firmware should create:

SD:/CARD-RAM/$remote_name

No overwrite is performed.

Continue?"; then
        return 0
    fi

    tmp_log="$(mktemp)"

    {
        echo "MiniJV880 Data Card upload"
        echo "=========================="
        echo
        echo "Host:   $HOST"
        echo "Local:  $file"
        echo "Remote: $remote_path"
        echo "Final:  SD:/CARD-RAM/$remote_name"
        echo "Size:   $digest_size bytes"
        echo "Digest: $digest_value"
        echo
    } > "$tmp_log"

    if run_tftp_put_with_progress \
        "$file" \
        "$remote_path" \
        "MiniJV880 Data Card upload" \
        "Uploading Data Card .bin image via TFTP..."; then

        if [ -n "$TFTP_OUTPUT" ]; then
            printf '%s\n' "$TFTP_OUTPUT" >> "$tmp_log"
            echo >> "$tmp_log"
        fi

        echo "OK: Data Card image uploaded via TFTP." >> "$tmp_log"
    else
        if [ -n "$TFTP_OUTPUT" ]; then
            printf '%s\n' "$TFTP_OUTPUT" >> "$tmp_log"
            echo >> "$tmp_log"
        fi

        echo "FAILED: Data Card upload failed." >> "$tmp_log"
    fi

    zenity \
        --text-info \
        --title="MiniJV880 Data Card upload result" \
        --width=820 \
        --height=480 \
        --filename="$tmp_log"

    rm -f "$tmp_log"
}

download_cardram_bin() {
    local remote_name remote_path output_file output_parent tmp_log
    local tmp_digest_error digest_info digest_status digest_size digest_value

    remote_name="$(select_remote_cardram_name)" || return 0

    if ! is_cardram_bin_remote_name "$remote_name"; then
        zenity --error \
            --width=620 \
            --title="MiniJV880 Data Card download" \
            --text="Invalid remote Data Card file name.

Use only one .bin file name, for example:

  INTERNAL.bin

Do not use:
  - empty names
  - . or ..
  - slashes
  - backslashes"
        return 1
    fi

    output_file="$(zenity \
        --file-selection \
        --save \
        --title="Save downloaded Data Card .bin as" \
        --filename="$remote_name")" || return 0

    if [ -z "$output_file" ]; then
        return 0
    fi

    if [ -d "$output_file" ]; then
        zenity --error \
            --width=620 \
            --title="MiniJV880 Data Card download" \
            --text="Selected output path is a directory:

$output_file"
        return 1
    fi

    output_parent="$(dirname -- "$output_file")"

    if [ ! -d "$output_parent" ]; then
        zenity --error \
            --width=620 \
            --title="MiniJV880 Data Card download" \
            --text="Output directory does not exist:

$output_parent"
        return 1
    fi

    remote_path="CARD-RAM/$remote_name"

    if ! zenity --question \
        --width=700 \
        --title="MiniJV880 Data Card download" \
        --text="This will download:

MiniJV880: $HOST
Remote:    SD:/$remote_path

and save it as:

$output_file

Expected file size:
32768 bytes

Continue?"; then
        return 0
    fi

    tmp_log="$(mktemp)"

    {
        echo "MiniJV880 Data Card download"
        echo "============================"
        echo
        echo "Host:   $HOST"
        echo "Remote: $remote_path"
        echo "Local:  $output_file"
        echo
    } > "$tmp_log"

    if run_tftp_get_with_progress \
        "$remote_path" \
        "$output_file" \
        "MiniJV880 Data Card download" \
        "Downloading Data Card .bin image via TFTP..."; then

        if [ -n "$TFTP_OUTPUT" ]; then
            printf '%s\n' "$TFTP_OUTPUT" >> "$tmp_log"
            echo >> "$tmp_log"
        fi

        tmp_digest_error="$(mktemp)"
        digest_info="$(get_file_digest_info "$output_file" 2>"$tmp_digest_error")"
        digest_status=$?

        if [ "$digest_status" -ne 0 ]; then
            {
                echo "FAILED: Data Card image was downloaded, but digest calculation failed."
                echo
                echo "Details:"
                cat "$tmp_digest_error" 2>/dev/null || true
            } >> "$tmp_log"

            rm -f "$tmp_digest_error"
        else
            rm -f "$tmp_digest_error"

            digest_size="${digest_info%% *}"
            digest_value="${digest_info#* }"

            {
                echo "Downloaded file:"
                echo "  Size:   $digest_size bytes"
                echo "  Digest: $digest_value"
                echo
            } >> "$tmp_log"

            if [ "$digest_size" = "32768" ]; then
                echo "OK: Data Card image downloaded and size is valid." >> "$tmp_log"
            else
                echo "WARNING: downloaded file size is not 32768 bytes." >> "$tmp_log"
            fi
        fi
    else
        if [ -n "$TFTP_OUTPUT" ]; then
            printf '%s\n' "$TFTP_OUTPUT" >> "$tmp_log"
            echo >> "$tmp_log"
        fi

        echo "FAILED: Data Card download failed." >> "$tmp_log"
    fi

    zenity \
        --text-info \
        --title="MiniJV880 Data Card download result" \
        --width=820 \
        --height=480 \
        --filename="$tmp_log"

    rm -f "$tmp_log"
}

upload_kernel() {
    local file tmp_log tmp_digest_error digest_info digest_status digest_size digest_value

    file="$(zenity \
        --file-selection \
        --title="Select kernel image to stage via TFTP")" || return 0

    tmp_digest_error="$(mktemp)"
    digest_info="$(get_file_digest_info "$file" 2>"$tmp_digest_error")"
    digest_status=$?

    if [ "$digest_status" -ne 0 ]; then
        zenity --error \
            --width=620 \
            --title="MiniJV880 kernel staging" \
            --text="Could not calculate MiniJV880 digest.

File:

$file

Details:

$(cat "$tmp_digest_error" 2>/dev/null || true)"
        rm -f "$tmp_digest_error"
        return 1
    fi

    rm -f "$tmp_digest_error"

    digest_size="${digest_info%% *}"
    digest_value="${digest_info#* }"

    if ! zenity --question \
        --width=680 \
        --title="MiniJV880 kernel staging" \
        --text="This will upload:

$file

File size:
$digest_size bytes

MiniJV880 digest:
$digest_value

using the remote TFTP name:

kernel8-rpi4.img

The firmware should stage it as:

SD:/kernel8-rpi4.img.new

This does NOT activate the kernel.
Use the HTTP kernel page afterwards to review and activate it.

Continue?"; then
        return 0
    fi

    tmp_log="$(mktemp)"

    {
        echo "MiniJV880 kernel staging"
        echo "========================"
        echo
        echo "Host:   $HOST"
        echo "Local:  $file"
        echo "Remote: kernel8-rpi4.img"
        echo "Size:   $digest_size bytes"
        echo "Digest: $digest_value"
        echo
    } > "$tmp_log"

    if run_tftp_put_with_progress \
        "$file" \
        "kernel8-rpi4.img" \
        "MiniJV880 kernel staging" \
        "Uploading kernel image via TFTP..."; then

        if [ -n "$TFTP_OUTPUT" ]; then
            printf '%s\n' "$TFTP_OUTPUT" >> "$tmp_log"
            echo >> "$tmp_log"
        fi

        {
            echo "OK: kernel image staged via TFTP."
            echo
            echo "Next step:"
            echo "Open the MiniJV880 HTTP kernel status/activate page."
        } >> "$tmp_log"
    else
        if [ -n "$TFTP_OUTPUT" ]; then
            printf '%s\n' "$TFTP_OUTPUT" >> "$tmp_log"
            echo >> "$tmp_log"
        fi

        echo "FAILED: kernel staging failed." >> "$tmp_log"
    fi

    zenity \
        --text-info \
        --title="MiniJV880 kernel staging result" \
        --width=820 \
        --height=480 \
        --filename="$tmp_log"

    rm -f "$tmp_log"
}

upload_ini() {
    local file tmp_log tmp_digest_error digest_info digest_status digest_size digest_value

    file="$(zenity \
        --file-selection \
        --title="Select minijv880.ini to stage via TFTP")" || return 0

    tmp_digest_error="$(mktemp)"
    digest_info="$(get_file_digest_info "$file" 2>"$tmp_digest_error")"
    digest_status=$?

    if [ "$digest_status" -ne 0 ]; then
        zenity --error \
            --width=620 \
            --title="MiniJV880 INI staging" \
            --text="Could not calculate MiniJV880 digest.

File:

$file

Details:

$(cat "$tmp_digest_error" 2>/dev/null || true)"
        rm -f "$tmp_digest_error"
        return 1
    fi

    rm -f "$tmp_digest_error"

    digest_size="${digest_info%% *}"
    digest_value="${digest_info#* }"

    if ! zenity --question \
        --width=680 \
        --title="MiniJV880 INI staging" \
        --text="This will upload:

$file

File size:
$digest_size bytes

MiniJV880 digest:
$digest_value

using the remote TFTP name:

minijv880.ini

The firmware should stage it as:

SD:/minijv880.ini.new

This does NOT apply the INI.
Use the HTTP INI page afterwards to review and apply it.

Continue?"; then
        return 0
    fi

    tmp_log="$(mktemp)"

    {
        echo "MiniJV880 INI staging"
        echo "====================="
        echo
        echo "Host:   $HOST"
        echo "Local:  $file"
        echo "Remote: minijv880.ini"
        echo "Size:   $digest_size bytes"
        echo "Digest: $digest_value"
        echo
    } > "$tmp_log"

    if run_tftp_put_with_progress \
        "$file" \
        "minijv880.ini" \
        "MiniJV880 INI staging" \
        "Uploading INI file via TFTP..."; then

        if [ -n "$TFTP_OUTPUT" ]; then
            printf '%s\n' "$TFTP_OUTPUT" >> "$tmp_log"
            echo >> "$tmp_log"
        fi

        {
            echo "OK: INI file staged via TFTP."
            echo
            echo "Next step:"
            echo "Open the MiniJV880 HTTP INI status/apply page."
        } >> "$tmp_log"
    else
        if [ -n "$TFTP_OUTPUT" ]; then
            printf '%s\n' "$TFTP_OUTPUT" >> "$tmp_log"
            echo >> "$tmp_log"
        fi

        echo "FAILED: INI staging failed." >> "$tmp_log"
    fi

    zenity \
        --text-info \
        --title="MiniJV880 INI staging result" \
        --width=820 \
        --height=480 \
        --filename="$tmp_log"

    rm -f "$tmp_log"
}

need_cmd zenity
need_cmd find
need_cmd sort
need_cmd python3
need_cmd curl
need_cmd tftp
need_executable "$TFTP_PUT"

ask_host

while true; do
    ACTION="$(zenity \
        --list \
        --title="MiniJV880 TFTP helper" \
        --text="Select one of the following actions:" \
        --width=640 \
        --height=420 \
        --ok-label="Run" \
        --cancel-label="Quit" \
        --column="Action" \
        "Upload .syx batch to PN-JV80 subfolder" \
        "Upload Data Card .bin to CARD-RAM" \
        "Download Data Card .bin from CARD-RAM" \
        "Stage kernel8-rpi4.img" \
        "Stage minijv880.ini" \
        "Calculate MiniJV880 digest for a file")"
    ACTION_STATUS=$?

    if [ "$ACTION_STATUS" -ne 0 ] || [ -z "$ACTION" ]; then
        exit 0
    fi

    case "$ACTION" in
        "Upload .syx batch to PN-JV80 subfolder")
            upload_syx_batch
            ;;
        "Upload Data Card .bin to CARD-RAM")
            upload_cardram_bin
            ;;
        "Download Data Card .bin from CARD-RAM")
            download_cardram_bin
            ;;
        "Stage kernel8-rpi4.img")
            upload_kernel
            ;;
        "Stage minijv880.ini")
            upload_ini
            ;;
        "Calculate MiniJV880 digest for a file")
            show_file_digest
            ;;
    esac
done
