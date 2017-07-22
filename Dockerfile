FROM ubuntu:xenial
MAINTAINER czmq Developers <zeromq-dev@lists.zeromq.org>

RUN DEBIAN_FRONTEND=noninteractive apt-get update -y -q
RUN DEBIAN_FRONTEND=noninteractive apt-get install -y -q --force-yes build-essential git-core libtool autotools-dev autoconf automake pkg-config unzip libkrb5-dev cmake sudo

RUN useradd -d /home/zmq -m -s /bin/bash zmq
RUN echo "zmq ALL=(ALL) NOPASSWD:ALL" > /etc/sudoers.d/zmq
RUN chmod 0440 /etc/sudoers.d/zmq

USER zmq

WORKDIR /home/zmq
RUN git clone --quiet https://github.com/zeromq/libzmq.git libzmq
WORKDIR /home/zmq/libzmq
RUN ./autogen.sh 2> /dev/null
RUN ./configure --quiet --without-docs
RUN make
RUN sudo make install
RUN sudo ldconfig

WORKDIR /home/zmq
RUN git clone --quiet --branch feat/curve git://github.com/wesyoung/czmq.git czmq
WORKDIR /home/zmq/czmq
RUN ./autogen.sh 2> /dev/null
RUN ./configure --quiet --without-docs
RUN make
RUN sudo make install
RUN sudo ldconfig

WORKDIR /home/zmq
RUN git clone --quiet --branch curve_merges git://github.com/JustinAzoff/zyre.git zyre
WORKDIR /home/zmq/zyre
RUN git pull #2017-07-17 -- 5
RUN ./autogen.sh 2> /dev/null
RUN ./configure --quiet --without-docs
RUN make
RUN sudo make install
RUN sudo ldconfig

RUN mkdir /home/zmq/simpledisco
WORKDIR /home/zmq/simpledisco
COPY . .
RUN make && make -f Makefile.gateway gateway.static
RUN ls -l client server gateway
