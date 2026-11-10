FROM python:3.12-slim

ENV PYTHONUNBUFFERED=1 \
    PYTHONDONTWRITEBYTECODE=1 \
    PIP_NO_CACHE_DIR=1 \
    PIP_DISABLE_PIP_VERSION_CHECK=1

RUN useradd --system --uid 10001 --user-group --no-create-home --shell /usr/sbin/nologin voice-hub \
    && install -d -o voice-hub -g voice-hub -m 0700 /data

COPY pyproject.toml /build/pyproject.toml
COPY src /build/src
RUN pip install --root-user-action=ignore /build && rm -rf /build

COPY docker-entrypoint.sh /usr/local/bin/docker-entrypoint.sh
RUN chmod 0755 /usr/local/bin/docker-entrypoint.sh

VOLUME /data
EXPOSE 8080

ENTRYPOINT ["docker-entrypoint.sh"]
CMD ["python", "-m", "voice_hub", "--data", "/data"]

HEALTHCHECK --interval=30s --timeout=5s --start-period=20s --retries=3 \
    CMD ["python", "-c", "import json, os, urllib.request as r; f = '/data/settings.json'; p = os.environ.get('VOICE_HUB_PORT', '').strip() or (json.load(open(f)).get('http_port', 8080) if os.path.exists(f) else 8080); r.urlopen(f'http://127.0.0.1:{p}/', timeout=3)"]
