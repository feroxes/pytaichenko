import base64
import itertools
import os
import shutil
import subprocess
from pathlib import Path

import pytest

FIRMWARE_DIR = Path(__file__).resolve().parents[1]
PROJECT_DIR = FIRMWARE_DIR.parent.parent
VENV_ESPHOME = PROJECT_DIR / ".venv" / "bin" / "esphome"
COPY_IGNORE = shutil.ignore_patterns(
    "secrets.yaml", ".esphome", "__pycache__", ".pytest_cache", "tests"
)
DUMMY_SECRETS = {
    "wifi_ssid": "dummy-ssid",
    "wifi_password": "dummy-password",
    "kitchen_api_key": base64.b64encode(bytes(range(32))).decode(),
    "kitchen_ota_password": "dummy-ota-password",
}


def esphome_binary() -> str:
    if VENV_ESPHOME.exists():
        return str(VENV_ESPHOME)
    found = shutil.which("esphome")
    if found is None:
        raise RuntimeError("esphome is not installed")
    return found


def write_dummy_secrets(config_dir: Path) -> None:
    lines = [f'{name}: "{value}"' for name, value in DUMMY_SECRETS.items()]
    (config_dir / "secrets.yaml").write_text("\n".join(lines) + "\n")


def render_config(
    root_name: str, with_secrets: bool, work_dir: Path, data_dir: Path
) -> str:
    config_dir = work_dir / "firmware"
    shutil.copytree(FIRMWARE_DIR, config_dir, ignore=COPY_IGNORE)
    if with_secrets:
        write_dummy_secrets(config_dir)
    env = dict(os.environ, ESPHOME_DATA_DIR=str(data_dir), NO_COLOR="1")
    result = subprocess.run(
        [esphome_binary(), "config", f"{root_name}.yaml"],
        cwd=config_dir,
        env=env,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise AssertionError(
            f"esphome config {root_name}.yaml failed ({result.returncode}):\n"
            f"{result.stderr}"
        )
    output = result.stdout
    for variant in {str(config_dir), str(config_dir.resolve())}:
        output = output.replace(variant, "<config_dir>")
    return output


@pytest.fixture(scope="session")
def esphome_data_dir(tmp_path_factory):
    return tmp_path_factory.mktemp("esphome-data")


@pytest.fixture
def esphome_config(tmp_path, esphome_data_dir):
    counter = itertools.count()

    def run(root_name: str, with_secrets: bool) -> str:
        work_dir = tmp_path / f"run{next(counter)}"
        work_dir.mkdir()
        return render_config(root_name, with_secrets, work_dir, esphome_data_dir)

    return run
