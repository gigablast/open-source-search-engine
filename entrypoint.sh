#!/bin/bash 

DATA_DIR=/var/gigablast/data0
PROTO_DIR=/proto

mkdir -p "$DATA_DIR"
if [ -z "$(ls -A $DATA_DIR)" ] ; then
  cp -a "$PROTO_DIR"/. "$DATA_DIR"
fi

exec "$@"
