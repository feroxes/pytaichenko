#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
target="$root/v1/firmware/secrets.yaml"

if [ -e "$target" ]; then
  exit 0
fi

py="$root/.venv/bin/python"
api_key="$("$py" -c 'import secrets,base64;print(base64.b64encode(secrets.token_bytes(32)).decode())')"
ota_password="$("$py" -c 'import secrets;print(secrets.token_hex(16))')"

umask 077
printf 'wifi_ssid: "CHANGE_ME"\nwifi_password: "CHANGE_ME"\nkitchen_api_key: "%s"\nkitchen_ota_password: "%s"\n' "$api_key" "$ota_password" > "$target"
chmod 600 "$target"
