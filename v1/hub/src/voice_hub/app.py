import argparse
import asyncio
import contextlib
import logging
import os
import signal
import sys
from collections.abc import Awaitable, Callable, Mapping, Sequence
from importlib.resources import files
from pathlib import Path

import aiohttp
from aiohttp import web

from voice_hub.api import Api, host_names
from voice_hub.audio import DEVICE_RATE, Resampler, read_wav_pcm
from voice_hub.config import ConfigError
from voice_hub.discovery import Discovery
from voice_hub.realtime import RealtimeSession
from voice_hub.registry import DeviceRegistry, Hub, link_factory
from voice_hub.settings import (PORT_ENV, SETTINGS_FILE, Settings, SettingsError, SettingsStore, check_server_port,
                                seed)
from voice_hub.streams import AudioStreams, local_ip

log = logging.getLogger(__name__)


class StartError(Exception):
    pass


SHUTDOWN_TIMEOUT_S = 5.0
NETWORK_RETRY_S = 2.0
PASSWORD_ENV = "VOICE_HUB_PASSWORD"
HOSTS_ENV = "VOICE_HUB_HOSTS"
PAGE_FILES = {
    "/": ("index.html", "text/html"),
    "/app.js": ("app.js", "text/javascript"),
    "/style.css": ("style.css", "text/css"),
    "/favicon.svg": ("favicon.svg", "image/svg+xml"),
}
PAGE_HEADERS = {
    "Content-Security-Policy": "default-src 'none'; script-src 'self'; style-src 'self'; img-src 'self'; "
                               "connect-src 'self'; form-action 'none'; frame-ancestors 'none'; base-uri 'none'",
    "X-Content-Type-Options": "nosniff",
    "X-Frame-Options": "DENY",
    "Referrer-Policy": "no-referrer",
    "Cache-Control": "no-cache",
}


def _page_file(body: bytes, content_type: str) -> Callable[[web.Request], Awaitable[web.Response]]:
    async def handler(request: web.Request) -> web.Response:
        return web.Response(body=body, content_type=content_type, charset="utf-8", headers=PAGE_HEADERS)

    return handler


def add_page(app: web.Application) -> None:
    folder = files("voice_hub") / "web"
    for path, (name, content_type) in PAGE_FILES.items():
        app.router.add_get(path, _page_file((folder / name).read_bytes(), content_type))


def build_app(streams: AudioStreams, devices_online: Callable[[], Mapping[str, bool]],
              api: Api | None = None) -> web.Application:
    async def health(request: web.Request) -> web.Response:
        devices = dict(devices_online())
        return web.json_response({"devices": devices}, status=200 if all(devices.values()) else 503)

    app = web.Application()
    app.router.add_get("/health", health)
    app.router.add_get("/audio/{stream_id}.wav", streams.handle)
    if api is not None:
        api.install(app)
        add_page(app)
    return app


def load_debug_input(path: Path) -> bytes:
    try:
        rate, pcm = read_wav_pcm(path)
    except (OSError, ValueError) as e:
        raise ConfigError(f"debug_input_wav: {e}") from e
    if rate == DEVICE_RATE:
        return pcm
    resampler = Resampler(rate, DEVICE_RATE)
    return resampler.process(pcm) + resampler.flush()


def port_from_env(value: str | None) -> int | None:
    text = (value or "").strip()
    if not text:
        return None
    number: int | str = text
    if text.isascii() and text.isdigit():
        with contextlib.suppress(ValueError):
            number = int(text)
    return check_server_port(number, PORT_ENV)


async def _own_address(stopping: asyncio.Event) -> str | None:
    warned = False
    while not stopping.is_set():
        try:
            return local_ip()
        except OSError as e:
            if not warned:
                log.warning("cannot find this server's address in the network yet (%s); trying again every %g s",
                            e.strerror or type(e).__name__, NETWORK_RETRY_S)
                warned = True
        with contextlib.suppress(TimeoutError):
            await asyncio.wait_for(stopping.wait(), NETWORK_RETRY_S)
    return None


async def run(store: SettingsStore, settings: Settings, password: str | None = None,
              hosts: frozenset[str] = frozenset(), env_port: int | None = None) -> None:
    debug_inputs = {d.debug_input_wav: load_debug_input(Path(d.debug_input_wav))
                    for d in settings.devices if d.debug_input_wav}
    port = env_port if env_port is not None else settings.http_port

    def debug_input(path: str) -> bytes:
        if path not in debug_inputs:
            debug_inputs[path] = load_debug_input(Path(path))
        return debug_inputs[path]

    loop = asyncio.get_running_loop()
    stopping = asyncio.Event()
    signals = (signal.SIGTERM, signal.SIGINT)
    for sig in signals:
        loop.add_signal_handler(sig, stopping.set)
    streams = AudioStreams()
    discovery = Discovery()
    try:
        host = settings.advertise_host or await _own_address(stopping)
        if host is None:
            return
        url_host = f"[{host}]" if ":" in host else host

        def audio_url(stream_id: str) -> str:
            return f"http://{url_host}:{port}/audio/{stream_id}.wav"

        async with aiohttp.ClientSession() as http:
            make_link = link_factory(lambda: hub.settings, streams, audio_url,
                                     lambda cfg: RealtimeSession(cfg, http), debug_input)
            hub = Hub(store, settings, DeviceRegistry(make_link))
            api = Api(hub, discovery, password, hosts, env_port=env_port)
            app = build_app(streams, hub.registry.health, api)
            runner = web.AppRunner(app, shutdown_timeout=SHUTDOWN_TIMEOUT_S)
            await runner.setup()
            try:
                await _listen(runner, port, store, env_port is not None)
                log.info("voice-hub listening on port %d, serving audio from %s", port, host)
                if password:
                    log.info("the page password comes from %s", PASSWORD_ENV)
                if env_port is not None:
                    log.info("the port comes from %s", PORT_ENV)
                await discovery.start()
                await hub.start()
                await stopping.wait()
                log.info("stopping voice-hub")
            finally:
                await hub.close()
                await discovery.stop()
                await runner.cleanup()
    finally:
        for sig in signals:
            loop.remove_signal_handler(sig)


async def _listen(runner: web.AppRunner, port: int, store: SettingsStore, from_env: bool) -> None:
    try:
        await web.TCPSite(runner, "0.0.0.0", port).start()
    except OSError as e:
        reason = e.strerror or type(e).__name__
        docker = "with Docker, in .env next to compose.yaml"
        fix = (f"choose another port in {PORT_ENV} ({docker})" if from_env else
               f"choose another port: set {PORT_ENV} ({docker}) or change http_port in {store.path}")
        raise StartError(f"port {port} cannot be used ({reason}); stop the program that holds it or {fix}") from None


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(prog="voice_hub")
    parser.add_argument("--data", type=Path, required=True)
    parser.add_argument("--config", type=Path)
    return parser.parse_args(argv)


def load_settings(store: SettingsStore, config_path: Path | None, env: Mapping[str, str],
                  env_port: int | None = None) -> Settings:
    settings = store.load(env_port)
    if settings is not None:
        if config_path is not None:
            log.info("using %s and ignoring %s", store.path, config_path)
        return settings
    settings = seed(config_path, env)
    store.save(settings)
    log.info("created %s from %s", store.path, config_path or "defaults")
    return settings


def main(argv: Sequence[str] | None = None, env: Mapping[str, str] | None = None) -> int:
    args = parse_args(argv)
    env = os.environ if env is None else env
    logging.basicConfig(level=logging.INFO, format="%(levelname)s %(name)s: %(message)s")
    try:
        port = port_from_env(env.get(PORT_ENV))
        store = SettingsStore(args.data / SETTINGS_FILE)
        settings = load_settings(store, args.config, env, port)
        hosts = host_names(env.get(HOSTS_ENV, "").split(","))
        asyncio.run(run(store, settings, env.get(PASSWORD_ENV) or None, hosts, port))
    except (ConfigError, SettingsError, StartError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    return 0
