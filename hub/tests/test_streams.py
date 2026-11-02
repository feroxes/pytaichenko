import asyncio

from aiohttp import web

from voice_hub.audio import wav_header
from voice_hub.streams import AudioStreams


def make_app(streams: AudioStreams) -> web.Application:
    app = web.Application()
    app.router.add_get("/audio/{stream_id}.wav", streams.handle)
    return app


async def test_stream_serves_header_then_audio(aiohttp_client):
    streams = AudioStreams()
    client = await aiohttp_client(make_app(streams))
    stream = streams.create()
    stream.write(b"\x01\x02")
    fetch = asyncio.create_task(client.get(f"/audio/{stream.id}.wav"))
    await asyncio.sleep(0.05)
    stream.write(b"\x03\x04")
    stream.finish()
    resp = await fetch
    assert resp.status == 200 and resp.headers["Content-Type"] == "audio/wav"
    assert "Content-Length" not in resp.headers
    assert await resp.read() == wav_header(16000) + b"\x01\x02\x03\x04"


async def test_headers_arrive_before_audio(aiohttp_client):
    streams = AudioStreams()
    client = await aiohttp_client(make_app(streams))
    stream = streams.create()
    resp = await asyncio.wait_for(client.get(f"/audio/{stream.id}.wav"), 1.0)
    assert resp.status == 200
    stream.finish()
    assert await resp.read() == wav_header(16000)


async def test_finished_before_fetch_still_served(aiohttp_client):
    streams = AudioStreams()
    client = await aiohttp_client(make_app(streams))
    stream = streams.create()
    stream.write(b"\x01\x02")
    stream.write(b"\x03\x04")
    stream.finish()
    assert stream.finished
    resp = await client.get(f"/audio/{stream.id}.wav")
    assert resp.status == 200
    assert await resp.read() == wav_header(16000) + b"\x01\x02\x03\x04"


async def test_unknown_stream_404(aiohttp_client):
    client = await aiohttp_client(make_app(AudioStreams()))
    resp = await client.get("/audio/nope.wav")
    assert resp.status == 404


async def test_stream_served_once(aiohttp_client):
    streams = AudioStreams()
    client = await aiohttp_client(make_app(streams))
    stream = streams.create()
    stream.finish()
    first = await client.get(f"/audio/{stream.id}.wav")
    assert first.status == 200
    await first.read()
    second = await client.get(f"/audio/{stream.id}.wav")
    assert second.status == 404


async def test_unfetched_streams_expire(aiohttp_client):
    streams = AudioStreams(ttl_s=0.05)
    client = await aiohttp_client(make_app(streams))
    old = streams.create()
    old.finish()
    await asyncio.sleep(0.1)
    streams.create()
    resp = await client.get(f"/audio/{old.id}.wav")
    assert resp.status == 404


async def test_a_stream_still_in_its_turn_is_not_purged(aiohttp_client):
    streams = AudioStreams(ttl_s=0.05)
    client = await aiohttp_client(make_app(streams))
    slow = streams.create()
    await asyncio.sleep(0.1)
    streams.create()
    slow.write(b"\x01\x02")
    await asyncio.sleep(0.1)
    streams.create()
    slow.finish()
    streams.create()
    resp = await client.get(f"/audio/{slow.id}.wav")
    assert resp.status == 200
    assert await resp.read() == wav_header(16000) + b"\x01\x02"


async def test_a_finished_stream_lives_its_time_from_the_turn_end(aiohttp_client):
    streams = AudioStreams(ttl_s=0.1)
    client = await aiohttp_client(make_app(streams))
    stream = streams.create()
    await asyncio.sleep(0.15)
    stream.finish()
    await asyncio.sleep(0.05)
    streams.create()
    assert (await client.get(f"/audio/{stream.id}.wav")).status == 200
    late = streams.create()
    late.finish()
    await asyncio.sleep(0.15)
    streams.create()
    assert (await client.get(f"/audio/{late.id}.wav")).status == 404


async def test_a_discarded_stream_ends_and_is_gone(aiohttp_client):
    streams = AudioStreams()
    client = await aiohttp_client(make_app(streams))
    stream = streams.create()
    stream.write(b"\x01\x02")
    streams.discard(stream)
    streams.discard(stream)
    assert stream.finished
    assert (await client.get(f"/audio/{stream.id}.wav")).status == 404


def test_stream_counts_written_bytes():
    stream = AudioStreams().create()
    assert stream.written == 0
    stream.write(b"\x01\x02\x03\x04")
    stream.write(b"")
    stream.finish()
    stream.write(b"\x05\x06")
    assert stream.written == 4
