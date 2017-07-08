#!/usr/bin/env python
import logging
import os
import sys
import random

import zmq
import zmq.auth
from zmq.auth.thread import ThreadAuthenticator
import time
import uuid

import json


def client():
    ctx = zmq.Context.instance()

    client = ctx.socket(zmq.REQ)

    # We need two certificates, one for the client and one for
    # the server. The client must know the server's public key
    # to make a CURVE connection.
    client_secret_file = os.path.join('private_keys', "client.key_secret")
    client_public, client_secret = zmq.auth.load_certificate(client_secret_file)
    client.curve_secretkey = client_secret
    client.curve_publickey = client_public

    server_public_file = os.path.join('public_keys', "server.key")
    server_public, _ = zmq.auth.load_certificate(server_public_file)
    # The client must know the server's public key to make a CURVE connection.
    client.curve_serverkey = server_public
    client.connect('tcp://127.0.0.1:9001')

    my_id = str(uuid.uuid4()).encode('utf-8')
    my_port = str(random.randint(2000,2020)).encode('utf-8')
    while True:
        client.send_multipart([b"PUBLISH", my_id, my_port])
        if not client.poll(2000):
            break
        msg = client.recv()
        print(msg)
        client.send_multipart([b"PEERS"])
        if not client.poll(2000):
            break
        peers = json.loads(client.recv().decode('utf-8'))

        for U, ep in peers:
            print("-", U, ep)
        time.sleep(1)
    print("Exit?")
    sys.exit(0)

if __name__ == '__main__':
    if zmq.zmq_version_info() < (4,0):
        raise RuntimeError("Security is not supported in libzmq version < 4.0. libzmq version {0}".format(zmq.zmq_version()))

    if '-v' in sys.argv:
        level = logging.DEBUG
    else:
        level = logging.INFO

    logging.basicConfig(level=level, format="[%(levelname)s] %(message)s")

    client()
