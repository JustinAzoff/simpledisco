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

def make_client(ctx, endpoint):
    client = ctx.socket(zmq.DEALER)
    client.connect(endpoint)
    return client

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
    client.connect(endpoint)
    
    return client

def client(server_endpoints, my_id, my_port):
    print("Client starting...")
    ctx = zmq.Context.instance()

    clients = {srv: make_client(ctx, srv) for srv in server_endpoints}

    while True:
        all_peers = {}
        for srv, client in clients.items():
            client.send_multipart([b"PUBLISH", my_id, my_port])
            if not client.poll(2000):
                print("DEAD:", srv)
                clients[srv] = make_client(ctx, srv)
                continue
            msg = client.recv()
            print(msg)
            client.send_multipart([b"VALUES"])
            if not client.poll(2000):
                print("DEAD:", srv)
                clients[srv] = make_client(ctx, srv)
                continue
            #peers = json.loads(client.recv().decode('utf-8'))
            print(client.recv())
            continue

            print("peers from", srv)
            for U, val in peers:
                print("-", U, val)
                all_peers[U] = val
            print()
            
        print("All peers:")
        for u, val in all_peers.items():
            print("-", u, val)
        print()

        time.sleep(4)

if __name__ == '__main__':
    if zmq.zmq_version_info() < (4,0):
        raise RuntimeError("Security is not supported in libzmq version < 4.0. libzmq version {0}".format(zmq.zmq_version()))

    if '-v' in sys.argv:
        level = logging.DEBUG
    else:
        level = logging.INFO

    logging.basicConfig(level=level, format="[%(levelname)s] %(message)s")

    servers = sys.argv[1:]

    my_id = str(uuid.uuid4()).encode('utf-8')
    my_port = str(random.randint(2000,2020)).encode('utf-8')
    while True:
        client(servers, my_id, my_port)
