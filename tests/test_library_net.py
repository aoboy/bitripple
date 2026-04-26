#!/usr/bin/env python3
"""Integration test: client encodes+sends, server receives+decodes over UDP."""
import ctypes
import os
import threading
import time

libpath = os.path.join(os.path.dirname(__file__), '..', 'libbitripple.so')
if not os.path.exists(libpath):
    libpath = './libbitripple.so'
lib = ctypes.CDLL(libpath)

BR_OK = 0


class br_profile_t(ctypes.Structure):
    _fields_ = [
        ("n", ctypes.c_uint32),
        ("m", ctypes.c_uint32),
        ("balls", ctypes.c_uint32 * 32),
        ("cum", ctypes.c_uint32 * 32),
        ("residual", ctypes.c_uint32),
    ]


lib.br_profile_new.argtypes = [ctypes.c_uint32, ctypes.c_uint32]
lib.br_profile_new.restype = ctypes.POINTER(br_profile_t)
lib.br_profile_free.argtypes = [ctypes.POINTER(br_profile_t)]
lib.br_profile_set_uniform.argtypes = [ctypes.POINTER(br_profile_t)]
lib.br_profile_set_uniform.restype = ctypes.c_int

lib.br_client_new.argtypes = [
    ctypes.POINTER(br_profile_t),
    ctypes.POINTER(ctypes.c_char_p),
    ctypes.POINTER(ctypes.c_uint16),
    ctypes.c_uint32,
    ctypes.c_uint16,
]
lib.br_client_new.restype = ctypes.c_void_p
lib.br_client_free.argtypes = [ctypes.c_void_p]
lib.br_client_send.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
lib.br_client_send.restype = ctypes.c_int

lib.br_server_new.argtypes = [ctypes.c_uint16, ctypes.c_size_t, ctypes.c_uint16]
lib.br_server_new.restype = ctypes.c_void_p
lib.br_server_free.argtypes = [ctypes.c_void_p]
lib.br_server_recv.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int]
lib.br_server_recv.restype = ctypes.c_int

SERVER_PORT = 13337
DATA = b"The quick brown fox jumps over the lazy dog. " * 50
SYM_SIZE = 64


def test_client_server():
    print("Test: network client/server integration")
    prof = lib.br_profile_new(2, 256)
    lib.br_profile_set_uniform(prof)

    result = {'ok': False, 'data': None}
    server_ready = threading.Event()

    def server_thread():
        srv = lib.br_server_new(SERVER_PORT, len(DATA), SYM_SIZE)
        assert srv, "server_new failed"
        server_ready.set()
        out = ctypes.create_string_buffer(len(DATA))
        rc = lib.br_server_recv(srv, out, len(DATA), 8000)
        if rc == BR_OK:
            result['ok'] = True
            result['data'] = bytes(out)
        lib.br_server_free(srv)

    t = threading.Thread(target=server_thread)
    t.start()
    server_ready.wait(timeout=2)
    time.sleep(0.2)

    hosts = (ctypes.c_char_p * 2)(b"127.0.0.1", b"127.0.0.1")
    ports = (ctypes.c_uint16 * 2)(SERVER_PORT, SERVER_PORT)
    client = lib.br_client_new(prof, hosts, ports, 2, SYM_SIZE)
    assert client, "client_new failed"

    rc = lib.br_client_send(client, DATA, len(DATA))
    assert rc == BR_OK, f"client_send failed: {rc}"
    lib.br_client_free(client)

    t.join(timeout=15)
    assert result['ok'], "server did not decode successfully"
    assert result['data'] == DATA, "decoded data mismatch"
    print("  PASS: client/server network round-trip OK")
    print(f"output data: {bytes(result['data'])}")

    lib.br_profile_free(prof)


if __name__ == '__main__':
    test_client_server()
    print("\nNetwork integration test passed.")