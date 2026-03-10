#!/usr/bin/env python3
"""Generate standard RFC 3610 AES-128-CCM MIC test vectors for Uguisu/Guillemot BLE immobilizer."""

import argparse
import binascii
from cryptography.hazmat.primitives.ciphers.aead import AESCCM


def le16(x: int) -> bytes:
    return bytes([x & 0xFF, (x >> 8) & 0xFF])


def le32(x: int) -> bytes:
    return bytes(
        [
            x & 0xFF,
            (x >> 8) & 0xFF,
            (x >> 16) & 0xFF,
            (x >> 24) & 0xFF,
        ]
    )


def parse_int(s: str) -> int:
    return int(s, 0)


def parse_key(hex_str: str) -> bytes:
    b = binascii.unhexlify(hex_str.strip())
    if len(b) != 16:
        raise ValueError("key must be 16 bytes (32 hex chars)")
    return b


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Generate standard RFC 3610 AES-128-CCM MIC for Uguisu/Guillemot test vectors."
    )
    ap.add_argument("--counter", type=parse_int, required=True)
    ap.add_argument("--command", type=parse_int, required=True)
    ap.add_argument(
        "--company-id",
        type=parse_int,
        default=0xFFFF,
        help="BLE MSD company ID, must match firmware (default 0xFFFF)",
    )
    ap.add_argument("--key", type=str, required=True, help="32 hex chars (16 bytes)")
    args = ap.parse_args()

    counter = args.counter & 0xFFFFFFFF
    command = args.command & 0xFF
    company_id = args.company_id & 0xFFFF

    # RFC 3610 Nonce: 13 bytes for L=2 (our case)
    # nRF52 SoftDevice/Bluefruit usually expects 13-byte nonces for CCM.
    nonce = le32(counter) + (b"\x00" * 9)
    
    # Split into AAD and Payload as per standard CCM usage in firmware
    aad = le32(counter)      # 4 bytes
    payload = bytes([command]) # 1 byte

    key = parse_key(args.key)
    aesccm = AESCCM(key, tag_length=8)
    
    # Encrypt using standard library (RFC 3610 compliant)
    # ct_and_tag = ciphertext(payload_len) + tag(8)
    ct_and_tag = aesccm.encrypt(nonce, payload, aad)
    
    ct = ct_and_tag[:-8]
    mic = ct_and_tag[-8:]

    # Resulting 13-byte payload for BLE advertisement:
    # AAD(4) + CT(1) + MIC(8)
    full_payload = aad + ct + mic
    msd = le16(company_id) + full_payload

    print("--- RFC 3610 CCM Vector ---")
    print(f"Key:      {key.hex()}")
    print(f"Nonce:    {nonce.hex()}")
    print(f"AAD:      {aad.hex()} (Counter)")
    print(f"Payload:  {payload.hex()} (Command)")
    print(f"CT:       {ct.hex()}")
    print(f"MIC:      {mic.hex()}")
    print(f"Full PL:  {full_payload.hex()} (13B)")
    print(f"MSD:      {msd.hex()}")
    print("---------------------------")


if __name__ == "__main__":
    main()
