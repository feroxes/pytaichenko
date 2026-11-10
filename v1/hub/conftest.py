import pytest

from fake_openai import FakeOpenAI


@pytest.fixture
async def fake_openai():
    fake = FakeOpenAI()
    await fake.start()
    yield fake
    await fake.stop()
