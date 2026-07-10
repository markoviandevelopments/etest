# Leonida Lights — Multiplayer

## How networking works (TCP over Cloudflare)

```
┌─────────────┐   cloudflared access tcp    ┌──────────────┐   QUIC/HTTPS :443   ┌─────────────┐
│ gta6_clone  │ ──────────────────────────► │ 127.0.0.1    │ ─────────────────► │ Cloudflare  │
│             │   connect local port only   │  :<proxy>    │                     │   edge      │
└─────────────┘                             └──────────────┘                     └──────┬──────┘
                                                                                    │
                                                                                    │ tunnel
                                                                                    ▼
                                                                          ┌─────────────────┐
                                                                          │ gta6_server     │
                                                                          │ 0.0.0.0:9043    │
                                                                          │ (origin only)   │
                                                                          └─────────────────┘
```

| What | Port / address |
|------|----------------|
| **Game server** (origin) | `0.0.0.0:9043` on the host PC |
| **cloudflared ingress** | `robot.… → tcp://localhost:9043` |
| **Public hostname** | `robot.immenseaccumulationonline.online` — **no public :9043** |
| **Client** | `cloudflared access tcp` → local port → game connects to **127.0.0.1** |

**Never** dial `robot.immenseaccumulationonline.online:9043`. That port is not open on Cloudflare’s edge (only 80/443). Origin `9043` exists only behind the tunnel.

TCP is preferred and used end-to-end for the game protocol. Cloudflare carries it inside the tunnel (not as a public raw TCP listener). UDP would need Spectrum or a different path.

## Run

**Server machine**
```bash
./gta6_server                 # listen :9043
# cloudflared tunnel already running with robot → tcp://localhost:9043
```

**Clients (any machine with cloudflared installed)**
```bash
./gta6_clone                  # starts Access TCP, then connects
# or
./play-online.sh
```

**Same machine as server**
```bash
./gta6_clone --local          # direct 127.0.0.1:9043
```

Console should look like:
```text
=== Cloudflare Tunnel (TCP) ===
  Public hostname : robot.immenseaccumulationonline.online  (edge HTTPS/443 — not :9043)
  Game connects to: 127.0.0.1:19xxx
Connecting to 127.0.0.1:19xxx  (via Access proxy) ...
Waiting for server WELCOME...
Joined as id=1 (...)
```
Then the game window opens (network is done **before** the black GL window).

## Build

```bash
sudo apt install build-essential pkg-config \
  libglfw3-dev libglew-dev libgl1-mesa-dev libfreetype6-dev cloudflared

cd etest && make
```
