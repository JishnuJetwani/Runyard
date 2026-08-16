#!/usr/bin/env bash
set -euo pipefail
# CI helper: install one verified version into the caller's temporary directory.
destination=${1:?destination directory required}
mkdir -p "$destination"
curl -fsSL https://dl.k8s.io/release/v1.34.1/bin/linux/amd64/kubectl -o "$destination/kubectl"
curl -fsSL https://dl.k8s.io/release/v1.34.1/bin/linux/amd64/kubectl.sha256 -o "$destination/kubectl.sha256"
(cd "$destination" && printf '%s  kubectl\n' "$(cat kubectl.sha256)" | sha256sum --check)
chmod +x "$destination/kubectl"
