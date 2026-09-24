"""Patch third-party libraries after PlatformIO installs them.

Runs as a pre: script, so lib_deps are already in .pio/libdeps. Each patch is
an exact text replacement that is skipped when already applied, and the build
stops if the library changed underneath it, rather than shipping unpatched.
"""

import os

Import("env")  # noqa: F821 - provided by PlatformIO

LIBDEPS = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"))  # noqa: F821

# ESPAsyncWebServer 3.6.0, webSocketSendFrame(): the frame header and payload
# went to TCP as two writes, and a failure after the header (payload write out
# of memory, or tcp_output failing) returned 0 as if nothing had been queued.
# The message then resent the whole frame, header included, behind the header
# already in the stream. The browser sees a header inside a payload, calls the
# frame invalid and closes the socket. Memory is tight with the tailnet client
# running, so this is how the page lost its connection. Now the frame is one
# write (all or nothing in lwIP), and once it is queued it counts as sent
# whether or not tcp_output managed to push it out yet.
WS_OLD = """  uint8_t* buf = (uint8_t*)malloc(headLen);
  if (buf == NULL) {
    // os_printf("could not malloc %u bytes for frame header\\n", headLen);
    //  Serial.println("SF 3");
    return 0;
  }
"""
WS_NEW = """  // PATCHED(rtk-base): header and payload in one write, see tools/patch_libs.py
  uint8_t* buf = (uint8_t*)malloc(headLen + len);
  if (buf == NULL) {
    return 0;
  }
"""
WS_OLD2 = """  if (client->add((const char*)buf, headLen) != headLen) {
    // os_printf("error adding %lu header bytes\\n", headLen);
    free(buf);
    // Serial.println("SF 4");
    return 0;
  }
  free(buf);

  if (len) {
    if (len && mask) {
      size_t i;
      for (i = 0; i < len; i++)
        data[i] = data[i] ^ mbuf[i % 4];
    }
    if (client->add((const char*)data, len) != len) {
      // os_printf("error adding %lu data bytes\\n", len);
      //  Serial.println("SF 5");
      return 0;
    }
  }
  if (!client->send()) {
    // os_printf("error sending frame: %lu\\n", headLen+len);
    //  Serial.println("SF 6");
    return 0;
  }
  // Serial.println("SF");
  return len;
}
"""
WS_NEW2 = """  if (len) {
    memcpy(buf + headLen, data, len);
    if (mask) {
      for (size_t i = 0; i < len; i++)
        buf[headLen + i] ^= mbuf[i % 4];
    }
  }
  size_t added = client->add((const char*)buf, headLen + len);
  free(buf);
  if (added != headLen + len) {
    // lwIP's tcp_write queues all of it or none of it
    return 0;
  }
  client->send();  // queued either way; lwIP retries the output itself
  return len;
}
"""

PATCHES = [
    ("ESPAsyncWebServer/src/AsyncWebSocket.cpp", [(WS_OLD, WS_NEW), (WS_OLD2, WS_NEW2)]),
]


def apply(rel, edits):
    path = os.path.join(LIBDEPS, rel)
    if not os.path.isfile(path):
        return
    with open(path, newline="") as f:
        text = f.read()
    changed = False
    for old, new in edits:
        if new in text:
            continue
        if old not in text:
            raise SystemExit("patch_libs: %s changed upstream, update tools/patch_libs.py" % rel)
        text = text.replace(old, new, 1)
        changed = True
    if changed:
        with open(path, "w", newline="") as f:
            f.write(text)
        print("patch_libs: patched %s" % rel)


for rel, edits in PATCHES:
    apply(rel, edits)
