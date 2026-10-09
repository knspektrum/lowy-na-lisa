"""Signs a firmware image for the BLE updater (LisekOta) and the web flasher.

    python3 sign.py keygen                      # once: new signing key + lisek_pubkey.h
    python3 sign.py sign <role> <version> <app.bin> <out.lsk>

The private key is $LISEK_SIGNING_KEY or ~/.config/lowy-na-lisa/signing-key.pem.
It never goes into the repository; devices only accept images signed with it.
Header layout matches struct Header in software/libraries/LisekOta/src/LisekOta.cpp.
"""
import hashlib
import os
import struct
import sys

from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import ec

KEY = os.environ.get("LISEK_SIGNING_KEY", os.path.expanduser("~/.config/lowy-na-lisa/signing-key.pem"))
PUBKEY_H = os.path.join(os.path.dirname(os.path.abspath(__file__)), "../libraries/LisekOta/src/lisek_pubkey.h")
HDR_LEN, SIGNED_LEN = 168, 92


def keygen():
    if os.path.exists(KEY):
        sys.exit(f"{KEY} already exists; refusing to overwrite it (devices trust that key)")
    key = ec.generate_private_key(ec.SECP256R1())
    os.makedirs(os.path.dirname(KEY), exist_ok=True)
    fd = os.open(KEY, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(fd, "wb") as f:
        f.write(key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                  serialization.NoEncryption()))
    der = key.public_key().public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)
    body = ",\n  ".join(", ".join(f"0x{b:02x}" for b in der[i:i + 12]) for i in range(0, len(der), 12))
    with open(PUBKEY_H, "w") as f:
        f.write("// Public half of the firmware signing key (made by software/ota/sign.py keygen).\n"
                "#pragma once\n#include <stdint.h>\n\n"
                f"static const uint8_t LISEK_PUBKEY_DER[] = {{\n  {body}\n}};\n")
    print(f"wrote {KEY} (back it up) and {os.path.relpath(PUBKEY_H)}")


def sign(role, version, image_path, out_path):
    with open(KEY, "rb") as f:
        key = serialization.load_pem_private_key(f.read(), None)
    image = open(image_path, "rb").read()
    signed = struct.pack("<4s12s40sI32s", b"LSK1", role.encode(), version.encode()[:39], len(image),
                         hashlib.sha256(image).digest())
    assert len(signed) == SIGNED_LEN
    sig = key.sign(signed, ec.ECDSA(hashes.SHA256()))
    hdr = signed + struct.pack("<B72s3x", len(sig), sig)
    assert len(hdr) == HDR_LEN
    with open(out_path, "wb") as f:
        f.write(hdr + image)
    print(f"signed {role} {version}: {len(image)} bytes -> {out_path}")


if __name__ == "__main__":
    if sys.argv[1:2] == ["keygen"]:
        keygen()
    elif sys.argv[1:2] == ["sign"] and len(sys.argv) == 6:
        sign(*sys.argv[2:])
    else:
        sys.exit(__doc__)
