#!/usr/bin/env python3
"""Host-side model of CredentialProtector enc:v2 round-trip semantics."""

from __future__ import annotations

import hashlib
import os
import re
import struct
from dataclasses import dataclass


PREFIX_V2 = "enc:v2:"
DEVICE_MAC = b"\x01\x02\x03\x04\x05\x06\x07\x08"
SALT = b"renzfi-cred-protect-v1"


@dataclass
class ProtectedParts:
    nonce_len: int = 0
    plain_len: int = 0
    cipher_len: int = 0
    tag_len: int = 0


def bytes_to_hex(data: bytes) -> str:
    return data.hex()


def hex_to_bytes(text: str) -> bytes:
    return bytes.fromhex(text)


def u32_to_hex(value: int) -> str:
    return bytes_to_hex(struct.pack(">I", value))


def hex_to_u32(text: str) -> int:
    return struct.unpack(">I", hex_to_bytes(text))[0]


def derive_key() -> bytes:
    return hashlib.sha256(DEVICE_MAC + SALT).digest()[:16]


def ctr_crypt(key: bytes, nonce: bytearray, data: bytes) -> bytes:
    """Deterministic stream cipher stand-in; mutates nonce like mbedtls_aes_crypt_ctr."""
    out = bytearray()
    stream_block = b""
    nc_offset = 0
    for byte in data:
        if nc_offset == 0:
            stream_block = hashlib.sha256(key + bytes(nonce)).digest()[:16]
            for idx in range(15, -1, -1):
                nonce[idx] = (nonce[idx] + 1) & 0xFF
                if nonce[idx]:
                    break
        out.append(byte ^ stream_block[nc_offset])
        nc_offset = (nc_offset + 1) % 16
    return bytes(out)


def protect_v2(plaintext: bytes, *, save_post_ctr_nonce: bool = False) -> tuple[str, ProtectedParts]:
    if not plaintext or len(plaintext) > 4096:
        raise ValueError("invalid plaintext length")

    key = derive_key()
    initial_nonce = bytearray(os.urandom(16))
    nonce_before_encrypt = bytes(initial_nonce)
    cipher = ctr_crypt(key, initial_nonce, plaintext)
    persisted_nonce = bytes(initial_nonce) if save_post_ctr_nonce else nonce_before_encrypt

    blob = (
        PREFIX_V2
        + bytes_to_hex(persisted_nonce)
        + ":"
        + u32_to_hex(len(plaintext))
        + ":"
        + bytes_to_hex(cipher)
    )
    parts = ProtectedParts(nonce_len=16, plain_len=len(plaintext), cipher_len=len(plaintext), tag_len=0)
    return blob, parts


def describe_blob(blob: str) -> ProtectedParts:
    if not blob.startswith(PREFIX_V2):
        raise ValueError("unsupported blob")

    body = blob[len(PREFIX_V2) :]
    nonce_hex, plain_len_hex, cipher_hex = body.split(":", 2)
    if len(nonce_hex) != 32 or len(plain_len_hex) != 8:
        raise ValueError("invalid header")

    plain_len = hex_to_u32(plain_len_hex)
    if plain_len == 0 or plain_len > 4096 or len(cipher_hex) != plain_len * 2:
        raise ValueError("invalid lengths")

    return ProtectedParts(nonce_len=16, plain_len=plain_len, cipher_len=plain_len, tag_len=0)


def unprotect_v2(blob: str) -> bytes:
    if not blob.startswith(PREFIX_V2):
        raise ValueError("unsupported blob")

    body = blob[len(PREFIX_V2) :]
    nonce_hex, plain_len_hex, cipher_hex = body.split(":", 2)
    initial_nonce = hex_to_bytes(nonce_hex)
    plain_len = hex_to_u32(plain_len_hex)
    cipher = hex_to_bytes(cipher_hex)
    if len(cipher) != plain_len:
        raise ValueError("cipher length mismatch")

    key = derive_key()
    plain = ctr_crypt(key, bytearray(initial_nonce), cipher)
    if len(plain) != plain_len:
        raise ValueError("plaintext length mismatch")
    return plain


def verify_round_trip(original: bytes, blob: str) -> tuple[bool, str]:
    try:
        describe_blob(blob)
    except ValueError:
        return False, "decode"
    try:
        recovered = unprotect_v2(blob)
    except ValueError:
        return False, "decrypt"
    if len(recovered) != len(original) or recovered != original:
        return False, "compare"
    return True, ""


def fingerprint(secret: bytes) -> str:
    return hashlib.sha256(secret).hexdigest()[:8]
