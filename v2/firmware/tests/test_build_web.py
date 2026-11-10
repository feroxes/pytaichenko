import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import pytest

FIRMWARE_DIR = Path(__file__).resolve().parents[1]
SCRIPT = FIRMWARE_DIR / "build-web.sh"
TOOL = FIRMWARE_DIR / "tools" / "web_image.py"
REAL_BUILD = FIRMWARE_DIR / "build"
REAL_FACTORY = REAL_BUILD / "dist" / "pytaichenko.factory.bin"

FAKE_ESPTOOL = """#!{python}
import os
import sys

args = sys.argv[1:]
with open(os.environ["FAKE_ESPTOOL_LOG"], "w") as handle:
    handle.write("\\n".join(args) + "\\n")
if os.environ.get("FAKE_ESPTOOL_FAIL"):
    print("merge failed", file=sys.stderr)
    sys.exit(3)
out = args[args.index("-o") + 1]
rest = args[args.index("--flash_freq") + 2 :]
data = bytearray()
for offset, path in zip(rest[::2], rest[1::2]):
    data += b"\\xff" * (int(offset, 16) - len(data))
    data += open(path, "rb").read()
open(out, "wb").write(bytes(data))
"""

PARTITIONS = """# Name, Type, SubType, Offset, Size
nvs,       data, nvs,     0xa000,   0x13000
otadata,   data, ota,     0x1d000,  0x2000
phy_init,  data, phy,     0x1f000,  0x1000
ota_0,     app,  ota_0,   0x30000,  1M
ota_1,     app,  ota_1,   ,         1M
"""

FLASHER_ARGS = {
    "write_flash_args": ["--flash_mode", "qio", "--flash_size", "8MB", "--flash_freq", "40m"],
    "flash_settings": {"flash_mode": "qio", "flash_size": "8MB", "flash_freq": "40m"},
    "flash_files": {
        "0x0": "bootloader/bootloader.bin",
        "0x30000": "pytaichenko.bin",
        "0x9000": "partition_table/partition-table.bin",
        "0x1d000": "ota_data_initial.bin",
    },
    "app": {"offset": "0x30000", "file": "pytaichenko.bin", "encrypted": "false"},
    "extra_esptool_args": {"chip": "esp32s3"},
}

EXPECTED_PARTS = [
    ("bootloader.bin", 0x0, "bootloader/bootloader.bin"),
    ("partition-table.bin", 0x9000, "partition_table/partition-table.bin"),
    ("ota_data_initial.bin", 0x1D000, "ota_data_initial.bin"),
    ("pytaichenko.bin", 0x30000, "pytaichenko.bin"),
]
PART_NAMES = sorted(name for name, _, _ in EXPECTED_PARTS)
SITE_FILES = sorted(PART_NAMES + ["manifest.json"])
DIST_FILES = ["pytaichenko.factory.bin", "pytaichenko.ota.bin"]


def esp_image(segments, tail=b""):
    header = bytes([0xE9, len(segments), 2, 0x2F]) + (0x40380000).to_bytes(4, "little")
    extended = bytes([0xEE] + [0] * 14 + [1])
    body = bytearray(header + extended)
    for index, payload in enumerate(segments):
        body += (0x3FC80000 + index * 0x1000).to_bytes(4, "little")
        body += len(payload).to_bytes(4, "little")
        body += payload
    while (len(body) + 1) % 16:
        body += b"\x00"
    body += b"\x5a"
    digest = bytes(range(32 - len(tail))) + tail
    return bytes(body + digest)


BOOTLOADER = esp_image([b"boot" * 300, b"\x01\x02\x03"])
APP = esp_image([b"app!" * 5000, b"x" * 77, b"\xff" * 40], tail=b"\xff\xff\xff")
ROWS = [
    (1, 0x02, 0xA000, 0x13000, "nvs"),
    (1, 0x00, 0x1D000, 0x2000, "otadata"),
    (1, 0x01, 0x1F000, 0x1000, "phy_init"),
    (0, 0x10, 0x30000, 0x100000, "ota_0"),
    (0, 0x11, 0x130000, 0x100000, "ota_1"),
]


def binary_table(rows, md5=True):
    entries = b"".join(
        b"\xaa\x50"
        + bytes([kind, subtype])
        + offset.to_bytes(4, "little")
        + size.to_bytes(4, "little")
        + label.encode().ljust(16, b"\x00")
        + bytes(4)
        for kind, subtype, offset, size, label in rows
    )
    if md5:
        entries += b"\xeb\xeb" + b"\xff" * 14 + hashlib.md5(entries).digest()
    return entries.ljust(0xC00, b"\xff")


TABLE = binary_table(ROWS)
OTADATA = b"\xff" * 0x2000
FILES = {
    "bootloader/bootloader.bin": BOOTLOADER,
    "partition_table/partition-table.bin": TABLE,
    "ota_data_initial.bin": OTADATA,
    "pytaichenko.bin": APP,
}


def expected_manifest(version, parts=None):
    parts = parts or [(name, offset) for name, offset, _ in EXPECTED_PARTS]
    return {
        "name": "Pytaichenko.Speaker",
        "version": version,
        "new_install_prompt_erase": True,
        "new_install_improv_wait_time": 15,
        "builds": [
            {
                "chipFamily": "ESP32-S3",
                "parts": [{"path": name, "offset": offset} for name, offset in parts],
            }
        ],
    }


@pytest.fixture
def tree(tmp_path):
    firmware = tmp_path / "repo" / "v2" / "firmware"
    (firmware / "tools").mkdir(parents=True)
    shutil.copy(SCRIPT, firmware / "build-web.sh")
    if TOOL.exists():
        shutil.copy(TOOL, firmware / "tools" / "web_image.py")
    (firmware / "VERSION").write_text("2.3.4-dev\n")
    (firmware / "partitions.csv").write_text(PARTITIONS)
    (firmware / "sdkconfig.defaults").write_text(
        'CONFIG_IDF_TARGET="esp32s3"\nCONFIG_PARTITION_TABLE_OFFSET=0x9000\n'
    )
    build = firmware / "build"
    for name, data in FILES.items():
        path = build / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
    (build / "flasher_args.json").write_text(json.dumps(FLASHER_ARGS, indent=4))
    fake_bin = tmp_path / "fake-bin"
    fake_bin.mkdir()
    esptool = fake_bin / "esptool.py"
    esptool.write_text(FAKE_ESPTOOL.format(python=sys.executable))
    esptool.chmod(0o755)
    return {
        "root": tmp_path,
        "repo": tmp_path / "repo",
        "firmware": firmware,
        "build": build,
        "dist": build / "dist",
        "log": tmp_path / "esptool.log",
        "fake_bin": fake_bin,
    }


def run(tree, *args, path=None, **env_extra):
    env = {
        "PATH": path
        or os.pathsep.join([str(tree["fake_bin"]), str(Path(sys.executable).parent), "/usr/bin", "/bin"]),
        "FAKE_ESPTOOL_LOG": str(tree["log"]),
    }
    env.update(env_extra)
    return subprocess.run(
        ["bash", str(tree["firmware"] / "build-web.sh"), *args],
        env=env,
        capture_output=True,
        text=True,
    )


def files_under(path):
    if not path.exists():
        return []
    return sorted(p.relative_to(path).as_posix() for p in path.rglob("*"))


def read_manifest(out):
    return json.loads((out / "manifest.json").read_text())


def set_flasher_args(tree, **changes):
    args = json.loads(json.dumps(FLASHER_ARGS))
    args.update(changes)
    (tree["build"] / "flasher_args.json").write_text(json.dumps(args))


def full_build(tree, out):
    result = run(tree, "--out", str(out))
    assert result.returncode == 0, result.stderr
    return tree["dist"] / "pytaichenko.factory.bin"


def assert_failed_cleanly(result, out, code=1):
    assert result.returncode == code
    assert len(result.stderr.strip().splitlines()) == 1, result.stderr
    assert files_under(out) == []


def test_full_build_puts_parts_on_the_site_and_images_in_dist(tree):
    out = tree["root"] / "out"
    full_build(tree, out)
    assert files_under(out) == SITE_FILES
    assert files_under(tree["dist"]) == DIST_FILES
    assert read_manifest(out) == expected_manifest("2.3.4")
    assert (tree["dist"] / "pytaichenko.ota.bin").read_bytes() == APP


def test_full_build_parts_equal_the_build_files(tree):
    out = tree["root"] / "out"
    full_build(tree, out)
    for name, _, source in EXPECTED_PARTS:
        assert (out / name).read_bytes() == FILES[source], name


def test_factory_image_holds_every_part_at_its_offset(tree):
    factory = full_build(tree, tree["root"] / "out")
    merged = factory.read_bytes()
    for _, offset, source in EXPECTED_PARTS:
        assert merged[offset : offset + len(FILES[source])] == FILES[source]


def test_dist_option_moves_the_images(tree):
    out = tree["root"] / "out"
    dist = tree["root"] / "deep" / "dist"
    result = run(tree, "--out", str(out), "--dist", str(dist))
    assert result.returncode == 0, result.stderr
    assert files_under(dist) == DIST_FILES
    assert not tree["dist"].exists()
    assert files_under(out) == SITE_FILES


def test_old_merged_images_are_removed_from_the_site_dir(tree):
    out = tree["root"] / "out"
    out.mkdir()
    (out / "pytaichenko.factory.bin").write_bytes(b"old")
    (out / "pytaichenko.ota.bin").write_bytes(b"old")
    full_build(tree, out)
    assert files_under(out) == SITE_FILES


def test_only_the_site_files_and_the_parts_of_the_old_manifest_are_cleared_from_the_out_dir(tree):
    out = tree["root"] / "out"
    out.mkdir()
    (out / "bootloader.bin").write_bytes(b"old")
    (out / "manifest.json").write_text("{}")
    (out / "notes.bin").write_bytes(b"keep")
    (out / "backup.json").write_text("keep")
    full_build(tree, out)
    assert files_under(out) == sorted(SITE_FILES + ["backup.json", "notes.bin"])
    assert (out / "notes.bin").read_bytes() == b"keep"
    assert (out / "backup.json").read_text() == "keep"
    assert (out / "bootloader.bin").read_bytes() == BOOTLOADER
    assert read_manifest(out) == expected_manifest("2.3.4")


def test_manifest_has_the_v1_keys_with_the_s3_chip(tree):
    out = tree["root"] / "out"
    full_build(tree, out)
    manifest = read_manifest(out)
    assert list(manifest) == [
        "name",
        "version",
        "new_install_prompt_erase",
        "new_install_improv_wait_time",
        "builds",
    ]
    assert list(manifest["builds"][0]) == ["chipFamily", "parts"]
    assert "home_assistant_domain" not in manifest


def test_merge_bin_gets_the_offsets_and_flash_settings_of_flasher_args(tree):
    full_build(tree, tree["root"] / "out")
    args = tree["log"].read_text().splitlines()
    assert args[:3] == ["--chip", "esp32s3", "merge_bin"]
    assert args[3] == "-o"
    assert Path(args[4]).name == "pytaichenko.factory.bin"
    assert args[5:11] == ["--flash_mode", "qio", "--flash_size", "8MB", "--flash_freq", "40m"]
    pairs = list(zip(args[11::2], args[12::2]))
    assert len(args[11:]) == 2 * len(EXPECTED_PARTS)
    assert [int(offset, 16) for offset, _ in pairs] == [offset for _, offset, _ in EXPECTED_PARTS]
    for (_, path), (_, _, source) in zip(pairs, EXPECTED_PARTS):
        assert Path(path).resolve() == (tree["build"] / source).resolve()


def test_other_flash_entries_become_parts_too(tree):
    (tree["build"] / "models.bin").write_bytes(b"models" * 100)
    files = dict(FLASHER_ARGS["flash_files"], **{"0x130000": "models.bin"})
    set_flasher_args(tree, flash_files=files)
    out = tree["root"] / "out"
    full_build(tree, out)
    parts = [(name, offset) for name, offset, _ in EXPECTED_PARTS] + [("models.bin", 0x130000)]
    assert read_manifest(out) == expected_manifest("2.3.4", parts)
    assert (out / "models.bin").read_bytes() == b"models" * 100


@pytest.mark.parametrize("offset", ["0xa000", "0x1c000", "0x9800"])
def test_a_flash_entry_over_nvs_fails_and_writes_nothing(tree, offset):
    (tree["build"] / "extra.bin").write_bytes(b"\x01" * 0x1000)
    files = dict(FLASHER_ARGS["flash_files"], **{offset: "extra.bin"})
    set_flasher_args(tree, flash_files=files)
    out = tree["root"] / "out"
    result = run(tree, "--out", str(out))
    assert_failed_cleanly(result, out)
    assert "nvs" in result.stderr
    assert not tree["log"].exists()


def test_manifest_only_lists_the_parts_from_partitions_csv_without_a_build(tree):
    shutil.rmtree(tree["build"])
    out = tree["root"] / "out"
    result = run(tree, "--manifest-only", "--out", str(out))
    assert result.returncode == 0, result.stderr
    assert files_under(out) == ["manifest.json"]
    assert read_manifest(out) == expected_manifest("2.3.4")


def test_manifest_only_matches_the_full_build_manifest(tree):
    full_out = tree["root"] / "full"
    full_build(tree, full_out)
    only_out = tree["root"] / "only"
    assert run(tree, "--manifest-only", "--out", str(only_out)).returncode == 0
    assert read_manifest(only_out) == read_manifest(full_out)


def test_manifest_only_fails_when_nvs_overlaps_a_part(tree):
    (tree["firmware"] / "partitions.csv").write_text(PARTITIONS.replace("0xa000", "0x9800"))
    out = tree["root"] / "out"
    result = run(tree, "--manifest-only", "--out", str(out))
    assert_failed_cleanly(result, out)
    assert "partition-table.bin" in result.stderr and "nvs" in result.stderr


def test_split_cuts_the_same_parts_out_of_the_factory_image(tree):
    factory = full_build(tree, tree["root"] / "full")
    saved = tree["root"] / "pytaichenko-2.3.4.factory.bin"
    shutil.copy(factory, saved)
    shutil.rmtree(tree["build"])
    out = tree["root"] / "split"
    result = run(tree, "--from-factory", str(saved), "--version", "2.3.4", "--out", str(out))
    assert result.returncode == 0, result.stderr
    assert files_under(out) == SITE_FILES
    for name, _, source in EXPECTED_PARTS:
        assert (out / name).read_bytes() == FILES[source], name
        assert (out / name).read_bytes() == (tree["root"] / "full" / name).read_bytes()
    assert read_manifest(out) == read_manifest(tree["root"] / "full")


def test_split_drops_fill_after_the_app_but_keeps_its_own_trailing_ff(tree):
    assert APP.endswith(b"\xff\xff\xff")
    factory = full_build(tree, tree["root"] / "full")
    filled = tree["root"] / "filled.bin"
    filled.write_bytes(factory.read_bytes() + b"\xff" * 0x5000)
    out = tree["root"] / "split"
    result = run(tree, "--from-factory", str(filled), "--version", "2.3.4", "--out", str(out))
    assert result.returncode == 0, result.stderr
    assert (out / "pytaichenko.bin").read_bytes() == APP


def test_split_writes_no_dist(tree):
    factory = full_build(tree, tree["root"] / "full")
    saved = tree["root"] / "saved.bin"
    shutil.copy(factory, saved)
    shutil.rmtree(tree["dist"])
    result = run(tree, "--from-factory", str(saved), "--version", "2.3.4", "--out", str(tree["root"] / "split"))
    assert result.returncode == 0, result.stderr
    assert not tree["dist"].exists()


def broken_factory(tree, change):
    factory = full_build(tree, tree["root"] / "full")
    data = bytearray(factory.read_bytes())
    change(data)
    broken = tree["root"] / "broken.bin"
    broken.write_bytes(bytes(data))
    return broken


@pytest.mark.parametrize(
    "change,words",
    [
        (lambda data: data.__setitem__(0, 0), "bootloader"),
        (lambda data: data.__setitem__(0x9000, 0), "partition table"),
        (lambda data: data.__setitem__(0x30000, 0), "app"),
        (lambda data: data.extend(b"\xff" * 10 + b"\x00"), "after the app"),
        (lambda data: data.__delitem__(slice(0x31000, None)), "cut short"),
    ],
)
def test_split_rejects_a_broken_factory_image(tree, change, words):
    broken = broken_factory(tree, change)
    out = tree["root"] / "split"
    result = run(tree, "--from-factory", str(broken), "--version", "2.3.4", "--out", str(out))
    assert_failed_cleanly(result, out)
    assert words in result.stderr


def factory_with_table(tree, rows, otadata_at, extra=None, md5=True):
    data = bytearray(b"\xff" * 0x30000)
    data[0 : len(BOOTLOADER)] = BOOTLOADER
    data[0x9000 : 0x9000 + 0xC00] = binary_table(rows, md5)
    data[otadata_at : otadata_at + len(OTADATA)] = OTADATA
    if extra:
        offset, payload = extra
        data[offset : offset + len(payload)] = payload
    data += APP
    image = tree["root"] / "made.bin"
    image.write_bytes(bytes(data))
    return image


def split(tree, image, out):
    return run(tree, "--from-factory", str(image), "--version", "2.3.4", "--out", str(out))


MOVED_ROWS = [
    (1, 0x02, 0xA000, 0x11000, "nvs"),
    (1, 0x00, 0x1B000, 0x2000, "otadata"),
    (1, 0x01, 0x1D000, 0x1000, "phy_init"),
    (0, 0x10, 0x30000, 0x100000, "ota_0"),
]


def test_split_takes_the_layout_from_the_table_inside_the_image_not_from_partitions_csv(tree):
    image = factory_with_table(tree, MOVED_ROWS, 0x1B000)
    out = tree["root"] / "split"
    result = split(tree, image, out)
    assert result.returncode == 0, result.stderr
    parts = [("bootloader.bin", 0x0), ("partition-table.bin", 0x9000), ("ota_data_initial.bin", 0x1B000), ("pytaichenko.bin", 0x30000)]
    assert read_manifest(out) == expected_manifest("2.3.4", parts)
    assert (out / "ota_data_initial.bin").read_bytes() == OTADATA
    assert (out / "partition-table.bin").read_bytes() == binary_table(MOVED_ROWS)
    assert (out / "pytaichenko.bin").read_bytes() == APP


def test_split_reads_a_table_without_an_md5_entry(tree):
    image = factory_with_table(tree, ROWS, 0x1D000, md5=False)
    out = tree["root"] / "split"
    result = split(tree, image, out)
    assert result.returncode == 0, result.stderr
    assert read_manifest(out) == expected_manifest("2.3.4")


def test_split_refuses_a_part_over_the_nvs_partition_of_the_image_table(tree):
    rows = [(1, 0x02, 0xA000, 0x14000, "nvs")] + ROWS[1:]
    image = factory_with_table(tree, rows, 0x1D000)
    out = tree["root"] / "split"
    result = split(tree, image, out)
    assert_failed_cleanly(result, out)
    assert "ota_data_initial.bin" in result.stderr and "nvs" in result.stderr


def test_split_refuses_an_unknown_image_between_the_ota_data_and_the_app(tree):
    image = factory_with_table(tree, ROWS, 0x1D000, extra=(0x20000, b"models"))
    out = tree["root"] / "split"
    result = split(tree, image, out)
    assert_failed_cleanly(result, out)
    assert "between the OTA data and the app" in result.stderr


@pytest.mark.parametrize(
    "rows,words",
    [
        ([row for row in ROWS if row[4] != "otadata"], "data partition of subtype ota\n"),
        ([row for row in ROWS if row[4] != "ota_0"], "app partition of subtype ota_0"),
    ],
)
def test_split_refuses_a_table_without_the_partitions_it_needs(tree, rows, words):
    image = factory_with_table(tree, rows, 0x1D000)
    out = tree["root"] / "split"
    result = split(tree, image, out)
    assert_failed_cleanly(result, out)
    assert words in result.stderr


def test_split_refuses_an_unreadable_table_entry(tree):
    factory = full_build(tree, tree["root"] / "full")
    data = bytearray(factory.read_bytes())
    data[0x9000 + 32 : 0x9000 + 34] = b"\x12\x34"
    broken = tree["root"] / "broken.bin"
    broken.write_bytes(bytes(data))
    out = tree["root"] / "split"
    result = split(tree, broken, out)
    assert_failed_cleanly(result, out)
    assert "partition table" in result.stderr


@pytest.mark.parametrize("mode", ["full", "split", "manifest"])
def test_old_part_files_are_cleared_from_the_output_dir(tree, mode):
    factory = full_build(tree, tree["root"] / "full")
    saved = tree["root"] / "saved.bin"
    shutil.copy(factory, saved)
    out = tree["root"] / "out"
    out.mkdir()
    (out / "models.bin").write_bytes(b"old")
    (out / "bootloader.bin").write_bytes(b"old")
    (out / "manifest.json").write_text(json.dumps({"builds": [{"parts": [{"path": "models.bin", "offset": 0x130000}, {"path": "../outside.bin", "offset": 0}]}]}))
    (out / "notes.txt").write_text("keep")
    (tree["root"] / "outside.bin").write_bytes(b"keep")
    args = {"full": ["--out", str(out)], "split": ["--from-factory", str(saved), "--version", "2.3.4", "--out", str(out)], "manifest": ["--manifest-only", "--out", str(out)]}[mode]
    result = run(tree, *args)
    assert result.returncode == 0, result.stderr
    expected = ["manifest.json"] if mode == "manifest" else PART_NAMES + ["manifest.json"]
    assert files_under(out) == sorted(expected + ["notes.txt"])
    assert (tree["root"] / "outside.bin").read_bytes() == b"keep"
    assert read_manifest(out) == expected_manifest("2.3.4")
    if mode != "manifest":
        assert (out / "bootloader.bin").read_bytes() == BOOTLOADER


def test_split_needs_a_version(tree):
    out = tree["root"] / "split"
    result = run(tree, "--from-factory", str(tree["build"] / "pytaichenko.bin"), "--out", str(out))
    assert_failed_cleanly(result, out)
    assert "--version" in result.stderr


def test_split_of_a_missing_file_fails(tree):
    out = tree["root"] / "split"
    result = run(tree, "--from-factory", str(tree["root"] / "nope.bin"), "--version", "2.0.0", "--out", str(out))
    assert_failed_cleanly(result, out)


def test_version_option_wins_over_the_version_file(tree):
    out = tree["root"] / "out"
    result = run(tree, "--version", "2.0.0", "--out", str(out))
    assert result.returncode == 0, result.stderr
    assert read_manifest(out)["version"] == "2.0.0"


def test_options_are_accepted_in_any_order(tree):
    out = tree["root"] / "out"
    result = run(tree, "--out", str(out), "--version", "2.1.0", "--manifest-only")
    assert result.returncode == 0, result.stderr
    assert files_under(out) == ["manifest.json"]
    assert read_manifest(out)["version"] == "2.1.0"


@pytest.mark.parametrize(
    "line,version",
    [("2.0.0-dev\n", "2.0.0"), ("2.5.1\n", "2.5.1"), ("2.0.0-rc.1", "2.0.0"), ("2.0.0-dev\r\n", "2.0.0")],
)
def test_version_file_suffix_is_stripped(tree, line, version):
    (tree["firmware"] / "VERSION").write_text(line)
    out = tree["root"] / "out"
    result = run(tree, "--manifest-only", "--out", str(out))
    assert result.returncode == 0, result.stderr
    assert read_manifest(out)["version"] == version


def test_output_directory_is_created(tree):
    out = tree["root"] / "deep" / "er" / "out"
    full_build(tree, out)
    assert (out / "manifest.json").is_file()


def test_default_output_is_site_firmware_v2(tree):
    result = run(tree)
    assert result.returncode == 0, result.stderr
    assert files_under(tree["repo"] / "site" / "firmware" / "v2") == SITE_FILES
    assert files_under(tree["dist"]) == DIST_FILES


def test_missing_build_fails_with_one_line_and_writes_nothing(tree):
    shutil.rmtree(tree["build"])
    out = tree["root"] / "out"
    result = run(tree, "--out", str(out))
    assert_failed_cleanly(result, out)
    assert "idf.py" in result.stderr
    assert not tree["log"].exists()


def test_missing_flashed_file_fails_and_writes_nothing(tree):
    (tree["build"] / "ota_data_initial.bin").unlink()
    out = tree["root"] / "out"
    result = run(tree, "--out", str(out))
    assert_failed_cleanly(result, out)
    assert "ota_data_initial.bin" in result.stderr


def test_missing_app_image_fails_and_writes_nothing(tree):
    set_flasher_args(tree, app={"offset": "0x30000", "file": "other.bin", "encrypted": "false"})
    out = tree["root"] / "out"
    result = run(tree, "--out", str(out))
    assert_failed_cleanly(result, out)
    assert "other.bin" in result.stderr


def test_failed_merge_writes_nothing(tree):
    out = tree["root"] / "out"
    result = run(tree, "--out", str(out), FAKE_ESPTOOL_FAIL="1")
    assert result.returncode != 0
    assert files_under(out) == []
    assert not tree["dist"].exists()


def test_existing_output_survives_a_failed_run(tree):
    out = tree["root"] / "out"
    full_build(tree, out)
    before = {name: (out / name).read_bytes() for name in files_under(out)}
    dist_before = {name: (tree["dist"] / name).read_bytes() for name in files_under(tree["dist"])}
    result = run(tree, "--out", str(out), "--version", "2.9.9", FAKE_ESPTOOL_FAIL="1")
    assert result.returncode != 0
    assert {name: (out / name).read_bytes() for name in files_under(out)} == before
    assert {name: (tree["dist"] / name).read_bytes() for name in files_under(tree["dist"])} == dist_before


@pytest.mark.parametrize("bad", ["2.0", "2.0.0-dev", "v2.0.0", "2.0.0.1", "two", ""])
def test_bad_version_option_fails_and_writes_nothing(tree, bad):
    out = tree["root"] / "out"
    result = run(tree, "--version", bad, "--out", str(out))
    assert_failed_cleanly(result, out)
    assert not tree["log"].exists()


@pytest.mark.parametrize("line", ["2.0\n", "dev\n", "\n", "2.0.0 \n"])
def test_bad_version_file_fails_and_writes_nothing(tree, line):
    (tree["firmware"] / "VERSION").write_text(line)
    out = tree["root"] / "out"
    result = run(tree, "--manifest-only", "--out", str(out))
    assert_failed_cleanly(result, out)


@pytest.mark.parametrize(
    "args",
    [
        ["--bogus"],
        ["--out"],
        ["--version"],
        ["--dist"],
        ["--from-factory"],
        ["extra"],
        ["--manifest-only", "--fast"],
        ["--manifest-only", "--dist", "x"],
        ["--from-factory", "f.bin", "--version", "2.0.0", "--manifest-only"],
        ["--from-factory", "f.bin", "--version", "2.0.0", "--dist", "x"],
    ],
)
def test_bad_arguments_print_usage(tree, args):
    result = run(tree, *args)
    assert result.returncode == 2
    assert "usage: build-web.sh" in result.stderr
    assert files_under(tree["repo"] / "site") == []


def test_missing_esptool_fails_with_one_line(tree):
    (tree["fake_bin"] / "esptool.py").unlink()
    out = tree["root"] / "out"
    result = run(tree, "--out", str(out), path=os.pathsep.join([str(tree["fake_bin"]), "/usr/bin", "/bin"]))
    assert_failed_cleanly(result, out)
    assert "esptool" in result.stderr


def test_build_for_another_chip_fails_and_writes_nothing(tree):
    set_flasher_args(tree, extra_esptool_args={"chip": "esp32"})
    out = tree["root"] / "out"
    result = run(tree, "--out", str(out))
    assert_failed_cleanly(result, out)
    assert "set-target esp32s3" in result.stderr
    assert not tree["log"].exists()


def test_unreadable_flasher_args_fails_with_one_line(tree):
    (tree["build"] / "flasher_args.json").write_text("{not json")
    out = tree["root"] / "out"
    result = run(tree, "--out", str(out))
    assert_failed_cleanly(result, out)
    assert "flasher_args.json" in result.stderr


def real_manifest(tmp_path):
    out = tmp_path / "out"
    result = subprocess.run(
        ["bash", str(SCRIPT), "--manifest-only", "--out", str(out)],
        env={"PATH": "/usr/bin:/bin"},
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stderr
    return json.loads((out / "manifest.json").read_text())


def real_partitions():
    rows = {}
    for line in (FIRMWARE_DIR / "partitions.csv").read_text().splitlines():
        fields = [field.strip() for field in line.split(",")]
        if len(fields) >= 5 and fields[3] and not fields[0].startswith("#"):
            size = fields[4]
            scale = {"K": 1024, "M": 1024 * 1024}.get(size[-1], 1)
            rows[fields[0]] = (int(fields[3], 0), int(size.rstrip("KM"), 0) * scale)
    return rows


def test_real_parts_never_touch_the_nvs_partition(tmp_path):
    rows = real_partitions()
    nvs_start, nvs_size = rows["nvs"]
    nvs_end = nvs_start + nvs_size
    parts = {part["path"]: part["offset"] for part in real_manifest(tmp_path)["builds"][0]["parts"]}
    assert parts == {
        "bootloader.bin": 0x0,
        "partition-table.bin": 0x8000,
        "ota_data_initial.bin": rows["otadata"][0],
        "pytaichenko.bin": rows["ota_0"][0],
    }
    lengths = {
        "bootloader.bin": 0x8000,
        "partition-table.bin": 0xC00,
        "ota_data_initial.bin": rows["otadata"][1],
        "pytaichenko.bin": rows["ota_0"][1],
    }
    for name, offset in parts.items():
        assert offset + lengths[name] <= nvs_start or offset >= nvs_end, name


@pytest.mark.skipif(not (REAL_BUILD / "flasher_args.json").exists(), reason="no local ESP-IDF build")
def test_real_build_flashes_what_the_manifest_lists(tmp_path):
    flasher = json.loads((REAL_BUILD / "flasher_args.json").read_text())
    listed = {Path(path).name: int(offset, 16) for offset, path in flasher["flash_files"].items()}
    parts = {part["path"]: part["offset"] for part in real_manifest(tmp_path)["builds"][0]["parts"]}
    assert parts == listed


@pytest.mark.skipif(not REAL_FACTORY.exists(), reason="no local web image build")
def test_real_factory_splits_back_into_the_build_files(tmp_path):
    out = tmp_path / "split"
    result = subprocess.run(
        ["bash", str(SCRIPT), "--from-factory", str(REAL_FACTORY), "--version", "2.0.0", "--out", str(out)],
        env={"PATH": "/usr/bin:/bin"},
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stderr
    flasher = json.loads((REAL_BUILD / "flasher_args.json").read_text())
    for path in flasher["flash_files"].values():
        assert (out / Path(path).name).read_bytes() == (REAL_BUILD / path).read_bytes(), path


def test_manifest_name_is_the_name_the_firmware_reports_over_improv(tmp_path):
    header = (FIRMWARE_DIR / "components/pt_core/include/pt_improv_proto.h").read_text()
    name = re.search(r'#define PT_IMPROV_FIRMWARE_NAME "([^"]+)"', header).group(1)
    manifest = real_manifest(tmp_path)
    assert manifest["name"] == name
    assert manifest["version"] == (FIRMWARE_DIR / "VERSION").read_text().strip().split("-")[0]


def test_scripts_have_no_comments():
    lines = SCRIPT.read_text().splitlines()
    assert lines[0] == "#!/usr/bin/env bash"
    assert not [line for line in lines[1:] if line.lstrip().startswith("#")]
    assert not [line for line in TOOL.read_text().splitlines() if line.lstrip().startswith("#")]
