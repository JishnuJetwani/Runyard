#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
umask 077
mkdir -p .secrets/tls
if [[ -e .secrets/tls/ca.key ]]; then echo 'Existing CA found; refusing to replace it.' >&2; exit 1; fi
openssl req -x509 -newkey rsa:2048 -nodes -days 365 -subj '/CN=Runyard Private CA' -keyout .secrets/tls/ca.key -out .secrets/tls/ca.crt
openssl req -new -newkey rsa:2048 -nodes -subj '/CN=server.runyard.svc' -keyout .secrets/tls/tls.key -out .secrets/tls/server.csr
cat > .secrets/tls/extensions.cnf <<'CONFIG'
subjectAltName=DNS:server.runyard.svc,DNS:server,DNS:localhost,IP:127.0.0.1
extendedKeyUsage=serverAuth
keyUsage=digitalSignature,keyEncipherment
basicConstraints=CA:FALSE
CONFIG
openssl x509 -req -in .secrets/tls/server.csr -CA .secrets/tls/ca.crt -CAkey .secrets/tls/ca.key -CAcreateserial -days 30 -extfile .secrets/tls/extensions.cnf -out .secrets/tls/tls.crt
