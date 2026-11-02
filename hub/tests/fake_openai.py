import asyncio
import json
from typing import Awaitable, Callable

from aiohttp import WSMsgType, web
from aiohttp.test_utils import TestServer
from multidict import CIMultiDict


class FakeOpenAI:
    def __init__(self) -> None:
        self.status: int | None = None
        self.reject_update = False
        self.send_created = True
        self.drop_on_connect = False
        self.on_message: Callable[[dict], Awaitable[None]] | None = None
        self.headers: CIMultiDict = CIMultiDict()
        self.query: dict[str, str] = {}
        self.received: list[dict] = []
        self.url = ""
        self._ws: web.WebSocketResponse | None = None
        self._server: TestServer | None = None
        self._connected = asyncio.Event()

    async def start(self) -> None:
        app = web.Application()
        app.router.add_get("/v1/realtime", self._handle)
        self._server = TestServer(app)
        await self._server.start_server()
        self.url = f"ws://{self._server.host}:{self._server.port}/v1/realtime"

    async def stop(self) -> None:
        await self.close_connection()
        if self._server is not None:
            await self._server.close()
            self._server = None

    async def wait_connected(self, timeout: float = 2.0) -> None:
        async with asyncio.timeout(timeout):
            await self._connected.wait()

    async def wait_received(self, count: int, timeout: float = 2.0) -> None:
        async with asyncio.timeout(timeout):
            while len(self.received) < count:
                await asyncio.sleep(0.005)

    async def push(self, msg: dict) -> None:
        assert self._ws is not None, "no client connected"
        await self._ws.send_json(msg)

    async def close_connection(self) -> None:
        ws, self._ws = self._ws, None
        if ws is not None and not ws.closed:
            await ws.close()

    async def _handle(self, request: web.Request) -> web.StreamResponse:
        self.headers = CIMultiDict(request.headers)
        self.query = dict(request.query)
        if self.status is not None:
            return web.Response(status=self.status)
        ws = web.WebSocketResponse()
        await ws.prepare(request)
        self._ws = ws
        self._connected.set()
        if self.drop_on_connect:
            await ws.close()
            return ws
        if self.send_created:
            await ws.send_json({"type": "session.created", "session": {"id": "sess_1", "model": self.query.get("model")}})
        async for frame in ws:
            if frame.type != WSMsgType.TEXT:
                continue
            msg = json.loads(frame.data)
            self.received.append(msg)
            if msg["type"] == "session.update":
                if self.reject_update:
                    await ws.send_json(
                        {
                            "type": "error",
                            "error": {
                                "type": "invalid_request_error",
                                "code": "invalid_value",
                                "message": "bad session",
                                "param": "session",
                                "event_id": "evt_1",
                            },
                        }
                    )
                else:
                    await ws.send_json({"type": "session.updated", "session": msg["session"]})
            if self.on_message is not None:
                await self.on_message(msg)
        return ws
