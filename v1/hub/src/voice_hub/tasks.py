import asyncio
import logging
from collections.abc import Callable, Coroutine, Iterable
from typing import Any

log = logging.getLogger(__name__)

Action = Callable[[], Coroutine[Any, Any, None]]


class Tasks:
    def __init__(self, owner: str) -> None:
        self._owner = owner
        self._running: set[asyncio.Task] = set()

    def spawn(self, coro: Coroutine[Any, Any, None], group: set[asyncio.Task] | None = None) -> asyncio.Task:
        task = asyncio.create_task(coro)
        self._running.add(task)
        task.add_done_callback(self._running.discard)
        task.add_done_callback(self._report_crash)
        if group is not None:
            group.add(task)
            task.add_done_callback(group.discard)
        return task

    async def close(self) -> None:
        current = asyncio.current_task()
        tasks = [task for task in self._running if task is not current]
        for task in tasks:
            task.cancel()
        await asyncio.gather(*tasks, return_exceptions=True)

    def _report_crash(self, task: asyncio.Task) -> None:
        if not task.cancelled() and task.exception() is not None:
            log.error("background task failed on %s", self._owner, exc_info=task.exception())


class Group:
    def __init__(self, tasks: Tasks) -> None:
        self._tasks = tasks
        self._running: set[asyncio.Task] = set()
        self._timer: asyncio.Task | None = None

    def spawn(self, coro: Coroutine[Any, Any, None]) -> asyncio.Task:
        return self._tasks.spawn(coro, self._running)

    def later(self, delay: float, action: Action) -> None:
        self.spawn(after(delay, action))

    def arm(self, delay: float, action: Action) -> None:
        if self._timer is not None and self._timer is not asyncio.current_task():
            self._timer.cancel()
        self._timer = self.spawn(after(delay, action))

    def cancel(self) -> None:
        cancel_others(self._running)


def cancel_others(tasks: Iterable[asyncio.Task | None]) -> None:
    current = asyncio.current_task()
    for task in list(tasks):
        if task is not None and task is not current:
            task.cancel()


async def after(delay: float, action: Action) -> None:
    await asyncio.sleep(delay)
    await action()
