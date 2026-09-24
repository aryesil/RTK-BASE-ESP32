# microlink (vendored)

Tailscale-compatible client implementing the ts2021 protocol, from
<https://github.com/CamM2325/microlink> (MIT, see `LICENSE`). Not affiliated
with or endorsed by Tailscale Inc.

Vendored rather than pulled as a component because it is modified. The changes
exist to make it fit an ESP32-WROOM-32D that is already running a full RTK base
station, on 320 kB of RAM with no PSRAM:

- `ml_coord.c` — the MapResponse path allocated `h2_recv`, `resp_buf` and a
  per-frame `frame_buf` at once, 192 kB live at the Kconfig minimum. Noise
  frames are now decrypted straight into the accumulation buffer and the HTTP/2
  payloads compacted in place, leaving one buffer. That buffer is claimed at
  startup by `ml_coord_alloc_map_buffer()` and held, because by the time a map
  poll runs the largest contiguous free block has fallen to about 51 kB.
- `ml_coord.c` — the registration response allocated a further 16 kB + 8 kB;
  both are now slices of the same startup buffer.
- `ml_coord.c` — the MapResponse is parsed section by section instead of as one
  cJSON tree. The tree for a 19 kB document exhausted the heap 4.8 kB in.
- `ml_coord.c` — `conn_window_delta` was computed unsigned, so a buffer below
  65535 wrapped it to ~4.29e9 and the resulting WINDOW_UPDATE overflowed the
  HTTP/2 connection window. The server answered GOAWAY FLOW_CONTROL_ERROR
  before reading anything, which looked exactly like a rejected auth key.
- `ml_derp.c` — the main loop delayed 1 ms, running about a thousand times a
  second at priority 5 and starving everything below it on core 0. Now 10 ms.
- `microlink_internal.h` — task stacks trimmed from 42 kB to 29 kB against
  measured usage, the DERP region table from 32 regions to 4 (17 kB to 2 kB),
  and every task pinned to core 0 so core 1 stays reserved for the GNSS serial
  path and the RTCM fan-out.
- `wireguard-platform.h` — `WIREGUARD_MAX_PEERS` from 16 to 6. The device
  struct embeds a full peer for every slot and is one allocation; at 16 it
  returned `ERR_MEM` and the tunnel interface never came up.

- `ml_coord.c` — the long-poll receive path is a byte-level state machine.
  Noise frames are at most 4 kB and HTTP/2 frames are cut wherever a Noise
  frame ends; the old loop treated every Noise frame as whole HTTP/2 frames,
  dropped whatever straddled a boundary and never returned those DATA bytes as
  flow-control credit. The stream window drained until the server could not
  even send keepalives and closed the connection (`coord_send failed
  errno=104`). It also asked for a 64 kB buffer per read, which this board never
  has contiguous, so often nothing was read and server PINGs went unanswered.
  It now reuses the startup map buffer and allocates nothing.
- `ml_coord.c` — outgoing requests are split into Noise frames of at most
  4096 bytes, the limit the control server enforces.
- `ml_coord.c` — the home DERP region is measured at boot (one STUN request per
  region, lowest round trip wins) instead of being hard-wired to Dallas, and
  the region table keeps the measured nearest four. Kept for the rest of the
  boot so the advertised region always matches the relay connection.
- `ml_coord.c` — an endpoint update is sent only when the STUN mapping changes
  (or every 10 minutes), not on every 23 s re-probe.
- `ml_coord.c`, `ml_derp.c` — `TCP_NODELAY` on the control and relay sockets.
- `ml_derp.c` — a DERP frame is written as one TLS record instead of two (the
  5-byte header used to be its own record, and Nagle then held the payload for
  a round trip); a header split across TLS records is completed instead of
  dropping the relay; a failed handshake frees its TLS state instead of
  leaking it; received packets are shifted in place instead of copied.
- `ml_derp.c`, `ml_net_io.c`, `ml_wg_mgr.c` — per-packet INFO logs are DEBUG.
  The one in the WireGuard UDP output ran inside the lwIP thread and held the
  whole network stack for the ~8 ms it took to print at 115200 baud.
- `ml_derp.c`, `ml_net_io.c`, `ml_wg_mgr.c` — the relay and WireGuard tasks are
  woken by task notification when a packet is queued for them rather than
  waiting out their 10 ms tick, each hop of the tunnel.
- `microlink_internal.h` — STUN receive queue 4 -> 8 (the region sweep), WG
  receive queue 8 -> 12 (a relay read burst).
- `ml_wg_mgr.c`, `wireguardif.c` — WireGuard state was driven from the WG
  manager task while the lwIP thread used the same state to encrypt, and
  decrypted packets went to `ip_input` from that task. A tunnel packet racing
  the lwIP thread panicked the board in `tcp_output` (LoadProhibited). Each
  of those drops looked like a stalled tunnel from outside. Every call into
  wireguard-lwip now holds the lwIP core lock (`CONFIG_LWIP_TCPIP_CORE_LOCKING`)
  and decrypted packets go through `netif->input`.
- `ml_wg_mgr.c`, `wireguardif.c` — a path change moves a live session to the
  new endpoint (`wireguardif_roam_endpoint`) instead of starting a handshake.
  When the LAN address and the router's public mapping both answered a probe,
  each answer started a handshake, and the second orphaned the first one's
  response. A candidate path replaces the current one only when it is a LAN
  address over a public one, a third faster, or the current path has expired.
  When the direct path expires, the session moves to the relay the same way.
- `ml_wg_mgr.c` — data for a session this board does not have (the peer kept
  its keys across our reboot) starts a handshake towards that peer. Before,
  every connection stalled for WireGuard's 15 s give-up time after a restart.
- `wireguard-platform-esp32.c` — handshake timestamps come from the wall clock
  once SNTP has set it (uptime before that). Peers refuse a timestamp older
  than the last they saw, so uptime-based ones were rejected after a reboot
  and the board could not start a session itself.
- `sdkconfig.defaults` — the relay's TLS session no longer keeps the server's
  parsed certificate chain after the handshake
  (`CONFIG_MBEDTLS_SSL_KEEP_PEER_CERTIFICATE=n`); about 6 kB more free heap
  and twice the largest free block with three browsers connected.
- `wireguardif.c` — its bare `printf`s go through `ESP_LOG` (tag
  `wireguardif`) so they obey the log level and the USB mute; the per-packet
  one is debug only.

Cellular, the bundled configuration web server and the network failover module
are not compiled; they are unused here and the first two pull in components
that do not build against this configuration.
