#!/bin/sh

cd "$(dirname "$0")"
export CURL_CA_BUNDLE="$PWD/cacert.pem"
exec ./immich.elf > "$LOGS_PATH/Immich.txt" 2>&1
