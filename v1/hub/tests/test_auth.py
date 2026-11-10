import base64

import pytest

from voice_hub import auth
from voice_hub.auth import SESSION_TTL_S, Sessions, hash_password, verify_password


class Clock:
    def __init__(self) -> None:
        self.now = 1000.0

    def __call__(self) -> float:
        return self.now


def test_hash_is_salted_scrypt_and_hides_the_password():
    first = hash_password("correct horse")
    second = hash_password("correct horse")
    assert first.startswith("scrypt$") and second.startswith("scrypt$")
    assert first != second
    assert "correct horse" not in first
    salt, digest = first.split("$")[-2:]
    assert len(base64.b64decode(salt)) == 16 and len(base64.b64decode(digest)) == 32


def test_verify_accepts_the_right_password_only():
    stored = hash_password("correct horse")
    assert verify_password("correct horse", stored) is True
    assert verify_password("correct horsE", stored) is False
    assert verify_password("", stored) is False


def test_verify_handles_other_scripts():
    stored = hash_password("пароль-кухні ✓")
    assert verify_password("пароль-кухні ✓", stored) is True
    assert verify_password("пароль-кухнi ✓", stored) is False


def test_verify_reads_the_cost_from_the_stored_hash(monkeypatch):
    monkeypatch.setattr(auth, "SCRYPT_N", 2 ** 10)
    stored = hash_password("correct horse")
    monkeypatch.undo()
    assert stored.split("$")[1] == str(2 ** 10)
    assert verify_password("correct horse", stored) is True


@pytest.mark.parametrize("stored", [
    "", "scrypt", "scrypt$16384$8$1$AAAA", "bcrypt$16384$8$1$AAAA$AAAA", "scrypt$x$8$1$AAAA$AAAA",
    "scrypt$1000$8$1$AAAAAAAAAAAAAAAAAAAAAA==$AAAA", "scrypt$16384$8$1$!!!$AAAA", "scrypt$16384$8$1$AAAA$",
    "scrypt$4194304$64$64$AAAAAAAAAAAAAAAAAAAAAA==$AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA=",
])
def test_verify_rejects_damaged_hashes(stored):
    assert verify_password("correct horse", stored) is False


def test_verify_rejects_text_that_is_not_utf8():
    assert verify_password("\ud800", hash_password("correct horse")) is False


def test_sessions_last_thirty_days():
    assert SESSION_TTL_S == 30 * 24 * 3600
    clock = Clock()
    sessions = Sessions(clock=clock)
    token = sessions.create()
    clock.now += SESSION_TTL_S - 1
    assert sessions.valid(token)
    clock.now += 2
    assert not sessions.valid(token)


def test_session_tokens_are_long_and_unique():
    sessions = Sessions()
    tokens = {sessions.create() for _ in range(50)}
    assert len(tokens) == 50 and all(len(t) >= 40 for t in tokens)


@pytest.mark.parametrize("token", [None, "", "nope"])
def test_unknown_tokens_are_not_valid(token):
    sessions = Sessions()
    sessions.create()
    assert not sessions.valid(token)


def test_drop_and_clear():
    sessions = Sessions()
    first, second, third = sessions.create(), sessions.create(), sessions.create()
    sessions.drop(first)
    sessions.drop(None)
    sessions.drop("nope")
    assert not sessions.valid(first) and sessions.valid(second) and sessions.valid(third)
    sessions.clear()
    assert not sessions.valid(second) and not sessions.valid(third)


def test_old_sessions_make_room_for_new_ones():
    clock = Clock()
    sessions = Sessions(clock=clock, limit=3)
    tokens = []
    for _ in range(4):
        tokens.append(sessions.create())
        clock.now += 1
    assert not sessions.valid(tokens[0])
    assert all(sessions.valid(t) for t in tokens[1:])


def test_expired_sessions_are_forgotten():
    clock = Clock()
    sessions = Sessions(clock=clock)
    old = sessions.create()
    clock.now += SESSION_TTL_S + 1
    sessions.create()
    assert len(sessions) == 1 and not sessions.valid(old)
