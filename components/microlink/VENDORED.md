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

Cellular, the bundled configuration web server and the network failover module
are not compiled; they are unused here and the first two pull in components
that do not build against this configuration.
