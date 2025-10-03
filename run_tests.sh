#!/usr/bin/env bash
# Simple smoke tests for myshell
set -euo pipefail
DIR=$(cd "$(dirname "$0")" && pwd)
cd "$DIR"
./myshell <<'EOF'
echo hello | tr a-z A-Z
sleep 0 &
jobs
exit
EOF

echo "Tests complete"
