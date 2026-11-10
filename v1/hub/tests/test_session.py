import asyncio
import time
from dataclasses import dataclass, field

import pytest

from fakes import FakeSession, SessionFactory, wait_for
from voice_hub.config import Timing
from voice_hub.realtime import InputCleared, RealtimeError
from voice_hub.session import RealtimeLike, SessionKeeper
from voice_hub.tasks import Tasks

OLD = Timing(first_turn_s=5.0, ready_check_s=1.0)
NEW = Timing(first_turn_s=0.2, ready_check_s=1.0)


@dataclass(eq=False)
class User:
    session: RealtimeLike | None = None
    cleared: asyncio.Event = field(default_factory=asyncio.Event)


class Keeper:
    def __init__(self, factory, timing, config=lambda: None) -> None:
        self.users: list[User] = []
        self.tasks = Tasks("kitchen")
        self.sessions = SessionKeeper("kitchen", factory, self.tasks, self.on_event, timing, config)

    async def on_event(self, session, event) -> None:
        if isinstance(event, InputCleared) and self.users:
            self.users[-1].cleared.set()

    async def turn(self) -> RealtimeLike:
        self.users.append(User())
        return await self.sessions.for_turn(self.users[-1], False)

    async def close(self) -> None:
        await self.sessions.close(self.sessions.detach())
        await self.tasks.close()


async def test_fixed_timing_is_used_as_is():
    k = Keeper(SessionFactory(), OLD)
    assert k.sessions.timing is OLD
    await k.turn()
    assert k.sessions.timing is OLD
    await k.close()


async def test_timing_is_read_fresh_while_no_session_is_open():
    current = [OLD]
    k = Keeper(SessionFactory(), lambda: current[0])
    assert k.sessions.timing is OLD
    current[0] = NEW
    assert k.sessions.timing is NEW
    await k.close()


async def test_changed_timing_opens_a_fresh_session_on_the_next_turn():
    current = [OLD]
    seen: list[Timing] = []
    factory = SessionFactory()

    def make() -> FakeSession:
        seen.append(current[0])
        return factory()

    k = Keeper(make, lambda: current[0])
    first = await k.turn()
    cid = k.sessions.cid
    assert await k.turn() is first and factory.calls == 1
    current[0] = NEW
    assert k.sessions.timing is OLD
    second = await k.turn()
    assert second is not first and factory.calls == 2
    assert seen == [OLD, NEW] and k.sessions.timing is NEW and k.sessions.cid != cid
    await wait_for(lambda: first.close_calls == 1)
    await k.close()


async def test_changed_openai_settings_open_a_fresh_session_on_the_next_turn():
    config = ["marin"]
    factory = SessionFactory()
    k = Keeper(factory, OLD, lambda: config[0])
    first = await k.turn()
    config[0] = "marin"
    assert await k.turn() is first and factory.calls == 1
    config[0] = "cedar"
    second = await k.turn()
    assert second is not first and factory.calls == 2
    await wait_for(lambda: first.close_calls == 1)
    assert await k.turn() is second and factory.calls == 2
    await k.close()


async def test_outdated_session_closes_in_the_background():
    current = [OLD]
    factory = SessionFactory()
    k = Keeper(factory, lambda: current[0])
    first = await k.turn()
    first.close_delay = 1.0
    current[0] = NEW
    started = time.monotonic()
    second = await k.turn()
    assert time.monotonic() - started < 0.5
    assert second is not first and factory.calls == 2
    await wait_for(lambda: first.close_calls == 1)
    await k.close()


async def test_fresh_timing_is_what_the_next_turn_will_use():
    current = [OLD]
    k = Keeper(SessionFactory(), lambda: current[0])
    await k.turn()
    current[0] = NEW
    assert k.sessions.timing is OLD and k.sessions.fresh_timing is NEW
    await k.turn()
    assert k.sessions.timing is NEW and k.sessions.fresh_timing is NEW
    await k.close()


async def test_dropped_session_forgets_what_the_idle_timer_would_forget():
    forgotten: list[str] = []
    k = Keeper(SessionFactory(), OLD)
    await k.turn()
    k.sessions.close_when_idle(5.0, lambda: forgotten.append("idle"))
    k.sessions.detach()
    assert forgotten == ["idle"]
    await k.close()
    assert forgotten == ["idle"]


async def test_idle_timer_without_a_session_forgets_at_once():
    forgotten: list[str] = []
    k = Keeper(SessionFactory(), OLD)
    k.sessions.close_when_idle(5.0, lambda: forgotten.append("idle"))
    assert forgotten == ["idle"]
    await k.close()


async def test_a_new_turn_cancels_the_idle_timer_without_forgetting():
    forgotten: list[str] = []
    k = Keeper(SessionFactory(), OLD)
    await k.turn()
    k.sessions.close_when_idle(0.05, lambda: forgotten.append("idle"))
    k.sessions.cancel_idle()
    await k.turn()
    await asyncio.sleep(0.1)
    assert forgotten == []
    await k.close()


async def test_idle_timer_forgets_once_and_closes_the_session():
    forgotten: list[str] = []
    factory = SessionFactory()
    k = Keeper(factory, OLD)
    session = await k.turn()
    k.sessions.close_when_idle(0.05, lambda: forgotten.append("idle"))
    await wait_for(lambda: session.close_calls == 1)
    assert forgotten == ["idle"]
    await k.close()
    assert forgotten == ["idle"]


async def test_failed_open_keeps_reading_fresh_timing():
    current = [OLD]
    k = Keeper(SessionFactory(FakeSession(fail_connect=True)), lambda: current[0])
    with pytest.raises(RealtimeError):
        await k.turn()
    current[0] = NEW
    assert k.sessions.timing is NEW
    await k.close()
