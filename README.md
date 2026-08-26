# Romero Screen Mirroring

**One binary.  Graphical UI.  Zero auth.  LAN only.**

Screen mirror, file transfer, and remote shell — all in a single cross-platform app. No accounts, no cloud, just point and use.

## Quick start

```bash
# Build
./scripts/build_linux.sh

# Launch the graphical UI
./build/romer
```

From the UI you can start the server, connect to a remote screen, transfer files, or open a shell.

## CLI reference (power users)

```
romer                             → graphical UI
romer --server [port] [fps] [q]   → headless daemon
romer <host>                      → quick-connect viewer
romer --shell <host>              → remote shell
romer --send <file> <host>        → push file
romer --recv <remote> <local> <host> → pull file
```

## Services (started from UI or --server)

| Port | Service | How to use |
|------|---------|-----------|
| 42817 | Screen mirror | Click "Connect" in UI, or `romer <host>` |
| 42818 | File transfer | Click "Send/Receive File", or `--send` / `--recv` |
| 42819 | Remote shell | Click "Remote Shell", or `--shell` |

## Build — Linux

### Debian / Ubuntu / Linux Mint / Pop!_OS / elementary OS
```bash
sudo apt install cmake g++ libjpeg-dev libx11-dev libxext-dev \
                 libsdl2-dev libsdl2-ttf-dev
./scripts/build_linux.sh
```

### Fedora / CentOS Stream / Rocky Linux / AlmaLinux
```bash
sudo dnf install cmake gcc-c++ libjpeg-turbo-devel libX11-devel libXext-devel \
                 SDL2-devel SDL2_ttf-devel
./scripts/build_linux.sh
```

### Arch Linux / Manjaro / EndeavourOS / Garuda
```bash
sudo pacman -S cmake gcc libjpeg-turbo libx11 libxext sdl2 sdl2_ttf
./scripts/build_linux.sh
```

### openSUSE Leap / Tumbleweed
```bash
sudo zypper install cmake gcc-c++ libjpeg-turbo-devel libX11-devel libXext-devel \
                     libSDL2-devel SDL2_ttf-devel
./scripts/build_linux.sh
```

### Gentoo
```bash
sudo emerge --ask cmake gcc media-libs/libjpeg-turbo x11-libs/libX11 \
                   x11-libs/libXext media-libs/libsdl2 media-libs/sdl2-ttf
./scripts/build_linux.sh
```

### Alpine Linux
```bash
sudo apk add cmake g++ make libjpeg-turbo-dev libx11-dev libxext-dev \
              sdl2-dev sdl2_ttf-dev
./scripts/build_linux.sh
```

### Void Linux
```bash
sudo xbps-install -S cmake gcc libjpeg-turbo-devel libX11-devel libXext-devel \
                      SDL2-devel SDL2_ttf-devel
./scripts/build_linux.sh
```

### NixOS / Nix
```bash
nix-shell -p cmake gcc libjpeg_turbo libX11 libXext SDL2 SDL2_ttf
./scripts/build_linux.sh
```

### Clear Linux
```bash
sudo swupd bundle-add dev-utils c-basic
# Build deps must be installed from source or via a third-party repo.
./scripts/build_linux.sh
```

### FreeBSD
```bash
pkg install cmake jpeg-turbo libX11 libXext sdl2 sdl2_ttf
./scripts/build_linux.sh
```

## Build — macOS

```bash
brew install cmake jpeg sdl2 sdl2_ttf
./scripts/build_mac.sh
```

## Build — Windows (vcpkg)

```powershell
vcpkg install libjpeg-turbo sdl2 sdl2-ttf
scripts\build_win.bat
```

## Project structure

```
src/
├── main.cpp              # Entry point – UI or CLI dispatch
├── ui.h / ui.cpp         # Graphical UI (SDL2 + SDL2_ttf)
├── capture.h             # Screen capture interface
├── capture_linux.cpp     # Linux: X11 + XShm
├── capture_mac.mm        # macOS: CoreGraphics
├── capture_win.cpp       # Windows: DXGI Duplication
├── jpeg_utils.h/.cpp     # libjpeg encode/decode
├── stream.h/.cpp         # TCP screen stream
├── file_transfer.h/.cpp  # Push/pull file transfer
├── remote_shell.h/.cpp   # PTY-based remote shell
└── protocol.h            # Ports & wire format
```

## ⚠ Security

**Zero auth.  Trusted LANs only.** Any device on the network can view your screen, read/write files, and execute commands. Do not expose ports 42817–42819 to the internet.

## Licence

MIT
