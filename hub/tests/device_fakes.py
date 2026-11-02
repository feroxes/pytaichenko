import asyncio
from dataclasses import replace

from aioesphomeapi import (
    APIConnectionError,
    DeviceInfo,
    InvalidEncryptionKeyAPIError,
    MediaPlayerInfo,
    VoiceAssistantConfigurationResponse,
    VoiceAssistantWakeWord,
)

PLAYER = MediaPlayerInfo(object_id="kitchen", key=7, name="")
WAKE_CONFIG = VoiceAssistantConfigurationResponse(
    available_wake_words=[
        VoiceAssistantWakeWord(id="okay_nabu", wake_word="Okay Nabu", trained_languages=["en"]),
        VoiceAssistantWakeWord(id="hey_mycroft", wake_word="Hey Mycroft", trained_languages=["en"]),
    ],
    active_wake_words=["okay_nabu"],
    max_active_wake_words=1,
)


class FakeClient:
    def __init__(self, entities=(), trace=None) -> None:
        self.entities = list(entities)
        self.trace = trace if trace is not None else []
        self.order: list[str] = []
        self.state_cb = None
        self.va_kwargs = None
        self.subscribe_calls = 0
        self.unsub_calls = 0
        self.events: list = []
        self.commands: list = []
        self.disconnects = 0
        self.fail_list: Exception | None = None
        self.fail_send: Exception | None = None
        self.wake_config = WAKE_CONFIG
        self.fail_config: Exception | None = None
        self.fail_set_config: Exception | None = None
        self.config_gate: asyncio.Event | None = None
        self.config_timeouts: list[float] = []
        self.config_cancelled = 0
        self.set_configs: list[list[str]] = []

    async def list_entities_services(self):
        if self.fail_list is not None:
            raise self.fail_list
        return list(self.entities), []

    def subscribe_states(self, on_state) -> None:
        self.state_cb = on_state

    def subscribe_voice_assistant(self, **kwargs):
        self.va_kwargs = kwargs
        self.subscribe_calls += 1
        self.order.append("subscribe")

        def unsub() -> None:
            self.unsub_calls += 1

        return unsub

    async def get_voice_assistant_configuration(self, timeout, external_wake_words=None):
        self.order.append("get_config")
        self.config_timeouts.append(timeout)
        if self.config_gate is not None:
            try:
                await self.config_gate.wait()
            except asyncio.CancelledError:
                self.config_cancelled += 1
                raise
        if self.fail_config is not None:
            raise self.fail_config
        return self.wake_config

    async def set_voice_assistant_configuration(self, active_wake_words) -> None:
        if self.fail_set_config is not None:
            raise self.fail_set_config
        self.set_configs.append(list(active_wake_words))
        self.wake_config = replace(self.wake_config, active_wake_words=list(active_wake_words))

    def send_voice_assistant_event(self, event, data) -> None:
        if self.fail_send is not None:
            raise self.fail_send
        self.events.append((event, data))

    def media_player_command(self, key, **kwargs) -> None:
        self.commands.append((key, kwargs))

    async def disconnect(self, force: bool = False) -> None:
        self.trace.append("client.disconnect")
        self.disconnects += 1


class FakeConversation:
    def __init__(self, device, trace=None) -> None:
        self.device = device
        self.trace = trace if trace is not None else []
        self.closed = False
        self.close_calls = 0
        self.gate: asyncio.Event | None = None

    async def handle_start(self, conversation_id, flags, audio_settings, wake_word_phrase):
        return 0

    async def handle_audio(self, data, data2=None) -> None:
        pass

    async def handle_stop(self, abort) -> None:
        pass

    async def close(self) -> None:
        self.close_calls += 1
        if self.gate is not None:
            await self.gate.wait()
        self.trace.append("conversation.close")
        self.closed = True


class FakeReconnect:
    instances: list = []

    def __init__(self, **kwargs) -> None:
        self.kwargs = kwargs
        self.trace = kwargs["client"].trace
        self.running_loop = asyncio.get_running_loop()
        FakeReconnect.instances.append(self)

    async def start(self) -> None:
        self.trace.append("reconnect.start")

    async def stop(self) -> None:
        self.trace.append("reconnect.stop")


class ConnectingReconnect(FakeReconnect):
    async def start(self) -> None:
        await super().start()
        await self.kwargs["on_connect"]()


class FakeClients:
    def __init__(self) -> None:
        self.made: list[tuple[tuple, dict, FakeClient]] = []

    def __call__(self, *args, **kwargs) -> FakeClient:
        client = FakeClient([PLAYER])
        self.made.append((args, kwargs, client))
        return client

    def last(self, host: str) -> FakeClient:
        return [client for args, _, client in self.made if args[0] == host][-1]


class AdoptClient:
    def __init__(self, info: DeviceInfo | None = None) -> None:
        self.info = info or DeviceInfo(name="speaker-a1b2c3", api_encryption_provisionable=True)
        self.calls: list = []
        self.keys: list[bytes] = []
        self.fail_connect: Exception | None = None
        self.hang_connect = False
        self.fail_set: Exception | None = None
        self.hang_set = False
        self.on_set = None
        self.accept = True

    async def connect(self, on_stop=None, login: bool = False, log_errors: bool = True) -> None:
        self.calls.append("connect")
        if self.hang_connect:
            await asyncio.Event().wait()
        if self.fail_connect is not None:
            raise self.fail_connect

    async def device_info(self) -> DeviceInfo:
        self.calls.append("device_info")
        return self.info

    async def noise_encryption_set_key(self, key: bytes) -> bool:
        self.calls.append("set_key")
        self.keys.append(key)
        if self.on_set is not None:
            self.on_set(key)
        if self.hang_set:
            await asyncio.Event().wait()
        if self.fail_set is not None:
            raise self.fail_set
        return self.accept

    async def disconnect(self, force: bool = False) -> None:
        self.calls.append("disconnect")


class AdoptFactory:
    def __init__(self, *clients: AdoptClient) -> None:
        self.clients = list(clients)
        self.client = self.clients[0]
        self.args: list[tuple] = []

    def __call__(self, host: str, port: int, key: str) -> AdoptClient:
        self.args.append((host, port, key))
        return self.clients[min(len(self.args), len(self.clients)) - 1]


def keyless_now() -> AdoptClient:
    return AdoptClient(DeviceInfo(name="speaker-a1b2c3", api_encryption_provisionable=True))


def keyed_now() -> AdoptClient:
    client = AdoptClient()
    client.fail_connect = InvalidEncryptionKeyAPIError("Invalid encryption key", "speaker-a1b2c3")
    return client


def gone_now() -> AdoptClient:
    client = AdoptClient()
    client.fail_connect = APIConnectionError("no route to host")
    return client
