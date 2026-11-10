#!/bin/sh
set -eu

if [ "$(id -u)" = "0" ]; then
    if ! chown -R voice-hub:voice-hub /data; then
        echo "error: cannot hand /data over to the voice-hub user (uid 10001). The data folder must be writable: do not mount it read-only, on SELinux hosts mount it as ./data:/data:z, or give it to uid 10001 on the host, for example: sudo chown -R 10001:10001 ./data" >&2
        exit 2
    fi
    exec setpriv --reuid=voice-hub --regid=voice-hub --init-groups "$@"
fi

exec "$@"
