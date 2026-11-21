# OpenMH

OpenMH is an engine, dedicated server and asset extractor for Digital Illusions
**Motorhead**, with Linux, Windows and WebAssembly builds.

## Build

Build release bundles using Docker with Buildx and Python 3:

```sh
./scripts/build_docker_linux_amd64
./scripts/build_docker_windows_amd64
./scripts/build_docker_wasm32
```

## Prepare and play

Use `openmotorex` to collect your Motorhead game files into an `.ass` package
that OpenMH can load.

Supported inputs:

- Physical CDs.
- Disc images: CUE, TOC or ISO. Keep BIN files beside their CUE/TOC file.
- Installed games: the game directory.
- Demo installers: supported `.exe` files.

From the directory containing the executables, create a **new** package and
launch it:

```sh
./openmotorex --compact motorhead.toc game.ass
./openmotor --assets game.ass
```

## Multiplayer

1. Start the server: `openmotorsv --assets game.ass`.
2. Connect from the same computer: `openmotor --assets game.ass --connect localhost`.

The server uses the OpenMH protocol on UDP port 16000. Use matching OpenMH
client and server builds. Native OpenMH joins prove a case-sensitive password
with a fresh challenge. The original Motorhead protocol sends passwords in
clear text. Original clients can connect directly to legacy servers, but their
one-packet discovery queries receive no roster.

- Remote players: add `--bind 0.0.0.0` to the server command and connect using
  the server’s address instead of `localhost`.
- Custom ports: use `--connect HOST:PORT` or enter `HOST:PORT` in the address
  book. Without a port, the client uses `16000`.
- Server settings: use an optional `server.cfg` to override defaults.
- Browser and native players can race together with the default WebRTC support.
  Local builds need OpenSSL 3 or later development files. Disable it with
  `-DMH_ENABLE_WEBRTC=OFF` in CMake when it is not needed.
