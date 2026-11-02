import base64
import binascii
import hashlib
import hmac
import secrets
import time
from collections.abc import Callable

SCRYPT_N = 2 ** 14
SCRYPT_R = 8
SCRYPT_P = 1
SCRYPT_MAXMEM = 64 * 1024 * 1024
SALT_BYTES = 16
HASH_BYTES = 32
SESSION_TTL_S = 30 * 24 * 3600
SESSION_LIMIT = 100
TOKEN_BYTES = 32


def _scrypt(password: str, salt: bytes, n: int, r: int, p: int, length: int) -> bytes:
    return hashlib.scrypt(password.encode("utf-8"), salt=salt, n=n, r=r, p=p, maxmem=SCRYPT_MAXMEM, dklen=length)


def hash_password(password: str) -> str:
    salt = secrets.token_bytes(SALT_BYTES)
    digest = _scrypt(password, salt, SCRYPT_N, SCRYPT_R, SCRYPT_P, HASH_BYTES)
    encoded = (base64.b64encode(part).decode("ascii") for part in (salt, digest))
    return "$".join(["scrypt", str(SCRYPT_N), str(SCRYPT_R), str(SCRYPT_P), *encoded])


def verify_password(password: str, stored: str) -> bool:
    try:
        kind, n, r, p, salt, digest = stored.split("$")
        if kind != "scrypt":
            return False
        expected = base64.b64decode(digest, validate=True)
        if not expected:
            return False
        actual = _scrypt(password, base64.b64decode(salt, validate=True), int(n), int(r), int(p), len(expected))
    except (ValueError, binascii.Error, MemoryError, OverflowError):
        return False
    return hmac.compare_digest(actual, expected)


class Sessions:
    def __init__(self, ttl_s: float = SESSION_TTL_S, clock: Callable[[], float] = time.monotonic,
                 limit: int = SESSION_LIMIT) -> None:
        self._ttl_s = ttl_s
        self._clock = clock
        self._limit = limit
        self._expires: dict[str, float] = {}

    def __len__(self) -> int:
        return len(self._expires)

    def create(self) -> str:
        now = self._clock()
        self._expires = {token: end for token, end in self._expires.items() if end > now}
        while len(self._expires) >= self._limit:
            del self._expires[min(self._expires, key=self._expires.__getitem__)]
        token = secrets.token_urlsafe(TOKEN_BYTES)
        self._expires[token] = now + self._ttl_s
        return token

    def valid(self, token: str | None) -> bool:
        if not token:
            return False
        end = self._expires.get(token)
        if end is None:
            return False
        if end <= self._clock():
            del self._expires[token]
            return False
        return True

    def drop(self, token: str | None) -> None:
        if token:
            self._expires.pop(token, None)

    def clear(self) -> None:
        self._expires.clear()
