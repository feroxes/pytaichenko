import argparse
import json
import os
import re
import subprocess
import sys

CHIP = "esp32s3"
NAME = "Pytaichenko.Speaker"
TABLE_LENGTH = 0xC00
TABLE_REGION = 0x1000
IMAGE_MAGIC = 0xE9
TABLE_MAGIC = b"\xaa\x50"
MD5_MAGIC = b"\xeb\xeb"
ENTRY_LENGTH = 32
TYPES = {0x00: "app", 0x01: "data"}
SUBTYPES = {"app": {0x00: "factory", 0x10: "ota_0", 0x11: "ota_1"}, "data": {0x00: "ota", 0x01: "phy", 0x02: "nvs"}}
FACTORY = "pytaichenko.factory.bin"
OTA = "pytaichenko.ota.bin"
BOOTLOADER = "bootloader.bin"
TABLE = "partition-table.bin"
OTADATA = "ota_data_initial.bin"
APP = "pytaichenko.bin"


class Failure(Exception):
    pass


def parse_size(text):
    text = text.strip()
    match = re.fullmatch(r"(0[xX][0-9a-fA-F]+|[0-9]+)([kKmM]?)", text)
    if not match:
        raise Failure(f"cannot read the size or offset '{text}' in partitions.csv")
    value = int(match.group(1), 0)
    unit = match.group(2).lower()
    return value * {"": 1, "k": 1024, "m": 1024 * 1024}[unit]


def table_offset(sdkconfig):
    if os.path.isfile(sdkconfig):
        with open(sdkconfig) as handle:
            for line in handle:
                match = re.match(r"CONFIG_PARTITION_TABLE_OFFSET=(\S+)", line.strip())
                if match:
                    return int(match.group(1), 0)
    return 0x8000


def read_partitions(csv_path, offset_of_table):
    if not os.path.isfile(csv_path):
        raise Failure(f"missing {csv_path}")
    partitions = []
    cursor = offset_of_table + TABLE_REGION
    with open(csv_path) as handle:
        for raw in handle:
            line = raw.strip()
            if not line or line.startswith("#"):
                continue
            fields = [field.strip() for field in line.split(",")]
            if len(fields) < 5:
                raise Failure(f"cannot read the line '{line}' in {csv_path}")
            name, kind, subtype, offset_text, size_text = fields[:5]
            align = 0x10000 if kind == "app" else 0x1000
            if offset_text:
                offset = parse_size(offset_text)
            else:
                offset = (cursor + align - 1) // align * align
            size = parse_size(size_text)
            partitions.append({"name": name, "type": kind, "subtype": subtype, "offset": offset, "size": size})
            cursor = offset + size
    return partitions


def find_partition(partitions, kind, subtype, source):
    for partition in partitions:
        if partition["type"] == kind and partition["subtype"] == subtype:
            return partition
    raise Failure(f"{source} has no {kind} partition of subtype {subtype}")


def read_binary_table(table, offset_of_table):
    partitions = []
    for start in range(0, len(table), ENTRY_LENGTH):
        entry = table[start : start + ENTRY_LENGTH]
        if entry.startswith(b"\xff\xff"):
            return partitions
        if entry.startswith(MD5_MAGIC):
            continue
        if not entry.startswith(TABLE_MAGIC) or len(entry) != ENTRY_LENGTH:
            raise Failure(f"cannot read entry {start // ENTRY_LENGTH} of the partition table at {offset_of_table:#x}")
        kind = TYPES.get(entry[2], f"{entry[2]:#x}")
        subtype = SUBTYPES.get(kind, {}).get(entry[3], f"{entry[3]:#x}")
        partitions.append(
            {
                "name": entry[12:28].split(b"\x00", 1)[0].decode("ascii", "replace"),
                "type": kind,
                "subtype": subtype,
                "offset": int.from_bytes(entry[4:8], "little"),
                "size": int.from_bytes(entry[8:12], "little"),
            }
        )
    raise Failure(f"the partition table at {offset_of_table:#x} has no end marker")


def nvs_ranges(partitions):
    return [
        (partition["offset"], partition["offset"] + partition["size"])
        for partition in partitions
        if partition["type"] == "data" and partition["subtype"] == "nvs"
    ]


def check_clear_of_nvs(parts, partitions):
    for name, offset, length in parts:
        for start, end in nvs_ranges(partitions):
            if offset < end and start < offset + length:
                raise Failure(
                    f"{name} at {offset:#x}-{offset + length - 1:#x} overlaps the nvs partition at {start:#x}-{end - 1:#x}"
                )


def layout(args):
    offset_of_table = table_offset(args.sdkconfig)
    partitions = read_partitions(args.csv, offset_of_table)
    otadata = find_partition(partitions, "data", "ota", args.csv)
    app = find_partition(partitions, "app", "ota_0", args.csv)
    return offset_of_table, partitions, otadata, app


def image_length(data, offset, what):
    if offset >= len(data) or data[offset] != IMAGE_MAGIC:
        raise Failure(f"no {what} image at {offset:#x} in the factory image")
    try:
        count = data[offset + 1]
        position = offset + 24
        for _ in range(count):
            position += 8 + int.from_bytes(data[position + 4 : position + 8], "little")
        position = (position + 1 + 15) // 16 * 16
        if data[offset + 23] == 1:
            position += 32
    except IndexError:
        raise Failure(f"the {what} image at {offset:#x} is cut short") from None
    if position > len(data):
        raise Failure(f"the {what} image at {offset:#x} is cut short")
    return position - offset


def manifest(version, parts):
    return {
        "name": NAME,
        "version": version,
        "new_install_prompt_erase": True,
        "new_install_improv_wait_time": 15,
        "builds": [
            {
                "chipFamily": "ESP32-S3",
                "parts": [{"path": name, "offset": offset} for name, offset, _ in parts],
            }
        ],
    }


def write_manifest(out, version, parts):
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "manifest.json"), "w") as handle:
        json.dump(manifest(version, parts), handle, indent=2)
        handle.write("\n")


def write_file(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as handle:
        handle.write(data)


def csv_parts(args):
    offset_of_table, partitions, otadata, app = layout(args)
    parts = [
        (BOOTLOADER, 0, offset_of_table),
        (TABLE, offset_of_table, TABLE_LENGTH),
        (OTADATA, otadata["offset"], otadata["size"]),
        (APP, app["offset"], app["size"]),
    ]
    check_clear_of_nvs(parts, partitions)
    return parts


def run_manifest(args):
    write_manifest(args.out, args.version, csv_parts(args))


def read_flasher_args(build):
    source = os.path.join(build, "flasher_args.json")
    try:
        with open(source) as handle:
            data = json.load(handle)
        settings = data["flash_settings"]
        values = [settings["flash_mode"], settings["flash_size"], settings["flash_freq"]]
        app = data["app"]["file"]
        files = sorted(data["flash_files"].items(), key=lambda item: int(item[0], 16))
        chip = data.get("extra_esptool_args", {}).get("chip")
    except (OSError, ValueError, KeyError, TypeError, AttributeError) as error:
        raise Failure(f"cannot read {source}: {error!r}") from None
    if chip != CHIP:
        raise Failure(f"{source} is a build for {chip}, not {CHIP}: run idf.py -C v2/firmware set-target {CHIP}")
    if not files:
        raise Failure(f"{source} lists no files to flash")
    return values, app, [(int(offset, 16), path) for offset, path in files]


def run_full(args):
    _, partitions, _, _ = layout(args)
    values, app_file, files = read_flasher_args(args.build)
    app_path = os.path.join(args.build, app_file)
    parts = []
    for offset, path in files:
        full = os.path.join(args.build, path)
        if not os.path.isfile(full):
            raise Failure(f"missing {full}: run idf.py -C v2/firmware build again")
        parts.append((os.path.basename(path), offset, os.path.getsize(full)))
    if not os.path.isfile(app_path):
        raise Failure(f"missing {app_path}: run idf.py -C v2/firmware build again")
    names = [name for name, _, _ in parts]
    if len(set(names)) != len(names) or {"manifest.json", FACTORY, OTA} & set(names):
        raise Failure(f"flasher_args.json has clashing file names: {', '.join(names)}")
    check_clear_of_nvs(parts, partitions)
    factory = os.path.join(args.dist, FACTORY)
    os.makedirs(args.dist, exist_ok=True)
    command = [args.esptool, "--chip", CHIP, "merge_bin", "-o", factory,
               "--flash_mode", values[0], "--flash_size", values[1], "--flash_freq", values[2]]
    for offset, path in files:
        command += [f"{offset:#x}", os.path.join(args.build, path)]
    result = subprocess.run(command)
    if result.returncode != 0:
        raise Failure(f"esptool merge_bin failed with exit code {result.returncode}")
    if not os.path.isfile(factory):
        raise Failure("esptool did not write the factory image")
    with open(factory, "rb") as handle:
        merged = handle.read()
    for name, offset, length in parts:
        piece = merged[offset : offset + length]
        if len(piece) != length:
            raise Failure(f"the factory image is too short for {name}")
        write_file(os.path.join(args.out, name), piece)
    with open(app_path, "rb") as handle:
        write_file(os.path.join(args.dist, OTA), handle.read())
    write_manifest(args.out, args.version, parts)


def run_split(args):
    offset_of_table = table_offset(args.sdkconfig)
    try:
        with open(args.image, "rb") as handle:
            merged = handle.read()
    except OSError as error:
        raise Failure(f"cannot read {args.image}: {error.strerror}") from None
    bootloader_length = image_length(merged, 0, "bootloader")
    if bootloader_length > offset_of_table:
        raise Failure(f"the bootloader runs past the partition table at {offset_of_table:#x}")
    table = merged[offset_of_table : offset_of_table + TABLE_LENGTH]
    if len(table) != TABLE_LENGTH or not table.startswith(TABLE_MAGIC):
        raise Failure(f"no partition table at {offset_of_table:#x} in the factory image")
    partitions = read_binary_table(table, offset_of_table)
    source = "the partition table in the factory image"
    otadata = find_partition(partitions, "data", "ota", source)
    app = find_partition(partitions, "app", "ota_0", source)
    ota = merged[otadata["offset"] : otadata["offset"] + otadata["size"]]
    if len(ota) != otadata["size"]:
        raise Failure(f"the factory image ends before the OTA data at {otadata['offset']:#x}")
    gap = merged[otadata["offset"] + otadata["size"] : app["offset"]]
    if gap.strip(b"\xff"):
        raise Failure(
            f"the factory image has data between the OTA data and the app ({otadata['offset'] + otadata['size']:#x}-{app['offset'] - 1:#x})"
        )
    app_length = image_length(merged, app["offset"], "app")
    if app_length > app["size"]:
        raise Failure(f"the app is larger than the {app['name']} partition")
    tail = merged[app["offset"] + app_length :]
    if tail.strip(b"\xff"):
        raise Failure(f"the factory image has data after the app at {app['offset'] + app_length:#x}")
    pieces = [
        (BOOTLOADER, 0, merged[:bootloader_length]),
        (TABLE, offset_of_table, table),
        (OTADATA, otadata["offset"], ota),
        (APP, app["offset"], merged[app["offset"] : app["offset"] + app_length]),
    ]
    parts = [(name, offset, len(data)) for name, offset, data in pieces]
    check_clear_of_nvs(parts, partitions)
    for name, _, data in pieces:
        write_file(os.path.join(args.out, name), data)
    write_manifest(args.out, args.version, parts)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=["manifest", "full", "split"])
    parser.add_argument("--csv", required=True)
    parser.add_argument("--sdkconfig", required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("--dist")
    parser.add_argument("--build")
    parser.add_argument("--esptool")
    parser.add_argument("--image")
    args = parser.parse_args()
    try:
        {"manifest": run_manifest, "full": run_full, "split": run_split}[args.mode](args)
    except Failure as failure:
        print(failure, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
