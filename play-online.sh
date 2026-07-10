#!/usr/bin/env bash
# Play Leonida Lights through the Cloudflare Tunnel TCP app.
# Server machine must run: ./gta6_server  +  cloudflared with tcp://localhost:9043
set -euo pipefail
cd "$(dirname "$0")"

HOST="${LEONIDA_HOST:-robot.immenseaccumulationonline.online}"
LOCAL_PORT="${LEONIDA_LOCAL_PORT:-19043}"

if [[ "${1:-}" == "--local" ]]; then
  exec ./gta6_clone --local "${@:2}"
fi

if ! command -v cloudflared >/dev/null 2>&1 && [[ ! -x /usr/local/bin/cloudflared ]]; then
  echo "cloudflared not found. Install it, or use: ./gta6_clone --local"
  exit 1
fi
CF="${CLOUDFLARED:-cloudflared}"
[[ -x /usr/local/bin/cloudflared ]] && CF=/usr/local/bin/cloudflared

echo "Starting Access TCP: $HOST  →  127.0.0.1:$LOCAL_PORT"
"$CF" access tcp --hostname "$HOST" --url "127.0.0.1:$LOCAL_PORT" &
CF_PID=$!
cleanup() { kill "$CF_PID" 2>/dev/null || true; }
trap cleanup EXIT

# Wait for proxy
for i in $(seq 1 50); do
  if (echo >/dev/tcp/127.0.0.1/"$LOCAL_PORT") 2>/dev/null; then
    break
  fi
  sleep 0.1
done

echo "Launching client → 127.0.0.1:$LOCAL_PORT"
exec ./gta6_clone 127.0.0.1 "$LOCAL_PORT" "$@"
