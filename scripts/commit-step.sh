#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <commit-message>" >&2
  exit 1
fi

git add .gitignore Makefile shell.nix src tests scripts
git commit -m "$1"
