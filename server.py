#!/usr/bin/env python

import logging
import os
import sys
from collections import namedtuple
import time
import json

import zmq
import zmq.auth
from zmq.auth.thread import ThreadAuthenticator
from zmq.auth.certs import load_certificates


Peer = namedtuple("Peer", "uuid endpoint last_seen")
PEER_TIMEOUT = 10

def clean(data):
    new_data = {}
    now = time.time()
    for peer in data.values():
        if now - peer.last_seen < PEER_TIMEOUT:
            new_data[peer.uuid] = peer
    return new_data

def dump_data(data):
    now = time.time()
    print()
    print("Peer list:")
    for uuid, peer in data.items():
        print("-",peer, "age is {:.2f}".format(now - peer.last_seen))
    print()

def server():
    ctx = zmq.Context.instance()
    # Start an authenticator for this context.
    auth = ThreadAuthenticator(ctx)
    auth.start()
    auth.allow('127.0.0.1')
    # Tell authenticator to use the certificate in a directory
    auth.configure_curve(domain='*', location='public_keys')

    server = ctx.socket(zmq.XREP)

    server_secret_file = os.path.join('private_keys', "server.key_secret")
    server_public, server_secret = zmq.auth.load_certificate(server_secret_file)
    server.curve_secretkey = server_secret
    server.curve_publickey = server_public
    server.curve_server = True  # must come before bind
    server.bind('tcp://*:9001')


    data = {}

    while True:
        #Does not auto reload like zcertstore
        trusted_keys = {k.decode('utf-8') for k in load_certificates("public_keys")}

        raw = server.recv_multipart(copy=False)
        md = {}
        for k in 'User-Id', 'Identity', 'Peer-Address':
            md[k] = raw[-1].get(k)
        print ("Meta is", md)

        if md['User-Id'] not in trusted_keys:
            print (md["User-Id"], "does not exist, ignoring")
            continue


        m = [x.bytes for x in raw]
        print("Received", m)

        ident, _, func, *args = m

        if func == b'PUBLISH':
            uuid, port = args
            endpoint = "tcp://{}:{}".format(md['Peer-Address'], port)
            data[uuid] = Peer(uuid.decode('utf-8'), endpoint, time.time())
            resp = b'OK'

        data = clean(data)
        dump_data(data)

        if func == b'PEERS':
            peers = [(p.uuid, p.endpoint) for p in data.values()]
            resp = json.dumps(peers).encode('utf-8')

        response = [ident, b'', resp]
        server.send_multipart(response)

if __name__ == "__main__":
    if zmq.zmq_version_info() < (4,0):
        raise RuntimeError("Security is not supported in libzmq version < 4.0. libzmq version {0}".format(zmq.zmq_version()))

    if '-v' in sys.argv:
        level = logging.DEBUG
    else:
        level = logging.INFO

    logging.basicConfig(level=level, format="[%(levelname)s] %(message)s")

    server()
