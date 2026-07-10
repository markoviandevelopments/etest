# Leonida Lights — GTA6-style Multiplayer (OpenGL)

Open-world sandbox with a dedicated TCP server and OpenGL client. Inspired by GTA VI aesthetics (not affiliated with Rockstar).

## Build

```bash
sudo apt install build-essential pkg-config libglfw3-dev libglew-dev libgl1-mesa-dev

cd etest
make
# produces: ./gta6_server  ./gta6_clone
```

## Server

Listens on **`0.0.0.0:9043`** by default:

```bash
./gta6_server
# or
./gta6_server 9043
./gta6_server 0.0.0.0 9043
```

### Cloudflare Tunnel (TCP)

Point your Cloudflare application / tunnel service URL at:

```text
tcp://localhost:9043
```

Then clients connect to the public hostname Cloudflare gives you (and port **9043** if that’s how the tunnel is exposed).

Example:

```bash
./gta6_clone your-tunnel-host.example.com 9043
```

> Note: the tunnel must be **TCP** (not HTTP). Cloudflare Spectrum or `cloudflared` TCP ingress is required for raw game TCP.

## Client

```bash
# Local server
./gta6_clone
./gta6_clone 127.0.0.1 9043

# Remote / tunnel
./gta6_clone <host> 9043

# Optional name
./gta6_clone --name Vic 127.0.0.1 9043

# Single-player (no server)
./gta6_clone --offline
```

If the server is unreachable, the client falls back to offline mode.

## Multiplayer features

- TCP binary protocol (`LLMP`) on port **9043**
- Server assigns player IDs, owns vehicle locks, runs traffic AI
- Clients send pose / owned-vehicle state @ ~20 Hz
- Server broadcasts world snapshot @ ~20 Hz
- Other players render as colored pedestrians; cars sync transforms
- Enter/exit vehicles with server-side ownership (`E`)

## Controls

| Key | Action |
|-----|--------|
| **WASD** / Arrows | Move (camera-relative) / drive |
| **Shift** | Run |
| **Mouse** / Scroll | Orbit / zoom |
| **E** | Enter / exit free vehicle |
| **Space** | Handbrake |
| **Esc** | Pause (frees mouse to move/resize window) |
| **Enter** | Resume |
| Pause → **Quit** | Exit client |
| **H** | Help panel |
| **Tab** | Free cursor while playing |

## Protocol (summary)

Length-prefixed packets: magic `LLMP`, type, size, payload.

| Type | Direction | Purpose |
|------|-----------|---------|
| `C_HELLO` | C→S | Player name |
| `S_WELCOME` | S→C | Player id + vehicle list |
| `C_STATE` | C→S | Pose + owned vehicle |
| `S_SNAPSHOT` | S→C | All players + vehicles |
| `C_ENTER` / `C_EXIT` | C→S | Vehicle claim / release |

## Layout

```
etest/
  Makefile
  src/
    main.cpp       # OpenGL client
    server.cpp     # headless multiplayer server
    protocol.hpp   # shared packet defs
    net.hpp        # TCP helpers
    camera.hpp mesh.hpp world.hpp player.hpp vehicle.hpp ...
```
