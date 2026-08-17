#include <network/Tailscale.h>
#include <microlink.h>
#include <microlink_internal.h>

static Preferences tsPrefs;
static microlink_t *ml = nullptr;
static uint32_t lastPollMs = 0;

// The client is started once and never stopped. That is a deliberate
// restriction, not an oversight:
//
//   microlink_stop() sets a shutdown bit and then blocks for three seconds
//   waiting for its four tasks to notice and delete themselves. Called from
//   the core 0 task, as an earlier version of this file did, that stalls
//   telemetry, the web server queue and the NTRIP push for three seconds at a
//   time - and the core 0 busy accounting counts the block as work, which is
//   why it read 99%.
//
//   Worse, the tasks do not reliably see the bit. The coordination task can be
//   parked in a socket read with a sixty second timeout while it waits for a
//   MapResponse, so it outlives the wait, keeps its stack, and the next start
//   allocates four more: 8 + 14 + 12 + 8 = 42 kB leaked per attempt. Two saves
//   were enough to take the heap down by the ~45 kB that was reported.
//
// microlink reads its settings once, at init, so restarting is the only way to
// apply a change anyway. Saving therefore stores the settings and asks for a
// reboot rather than pretending it can swap them live.
static bool started = false;

void loadTailscaleCfg() {
  tsPrefs.begin("tscale", false);
  tsCfg.enabled = tsPrefs.getBool("en", false);
  strlcpy(tsCfg.authKey, tsPrefs.getString("key", "").c_str(), sizeof(tsCfg.authKey));
  strlcpy(tsCfg.devName, tsPrefs.getString("name", "rtk-base").c_str(), sizeof(tsCfg.devName));
  if (tsCfg.devName[0] == '\0') strlcpy(tsCfg.devName, "rtk-base", sizeof(tsCfg.devName));
}

void saveTailscaleCfg() {
  tsPrefs.putBool("en", tsCfg.enabled);
  tsPrefs.putString("key", tsCfg.authKey);
  tsPrefs.putString("name", tsCfg.devName);
  // Only meaningful once something is actually running; before that the new
  // settings are picked up by the normal start path.
  if (started) tsStatus.needReboot = true;
}

void initTailscale() {
  loadTailscaleCfg();

  // Claimed now, during setup(), and held for the life of the program. The
  // MapResponse needs one contiguous block, and by the time the web server and
  // its sockets are running the largest free block on this board has fallen to
  // about 51 kB - measured, not assumed. Asking for it later fails even though
  // the total free heap still looks ample.
  tsStatus.bufReady = (ml_coord_alloc_map_buffer() == ESP_OK);
  if (!tsStatus.bufReady) {
    strlcpy(tsStatus.msg, "No memory for the map buffer", sizeof(tsStatus.msg));
    Log.println("[TS] Map buffer allocation failed; client unavailable.");
    return;
  }
  Log.printf("[TS] Map buffer %u bytes, client state %u bytes\n",
             (unsigned)ML_H2_BUFFER_SIZE, (unsigned)microlink_state_size());

  if (!tsCfg.enabled) strlcpy(tsStatus.msg, "Disabled", sizeof(tsStatus.msg));
}

void handleTailscale() {
  if (!tsStatus.bufReady || tsStatus.failed) return;

  if (!started) {
    if (!tsCfg.enabled) {
      strlcpy(tsStatus.msg, "Disabled", sizeof(tsStatus.msg));
      return;
    }
    if (tsCfg.authKey[0] == '\0') {
      strlcpy(tsStatus.msg, "Auth key required", sizeof(tsStatus.msg));
      return;
    }
    // The coordination server is on the internet; the soft-AP has no route off
    // the device. Nothing is torn down if the uplink drops later - microlink
    // reconnects on its own - but there is no point starting without one.
    if (currentNetState != NET_STA) {
      strlcpy(tsStatus.msg, "Needs a WiFi uplink (Network tab)", sizeof(tsStatus.msg));
      return;
    }

    microlink_config_t cfg = {};
    cfg.auth_key     = tsCfg.authKey;   // outlives the call: it lives in tsCfg
    cfg.device_name  = tsCfg.devName;
    cfg.enable_derp  = true;            // the only path that survives CGNAT
    cfg.enable_stun  = true;
    cfg.enable_disco = true;
    cfg.max_peers    = ML_MAX_PEERS;

    ml = microlink_init(&cfg);
    if (!ml) {
      // Latched. Retrying would allocate another peer table on every pass and
      // there is nothing here that changes between attempts.
      tsStatus.failed = true;
      strlcpy(tsStatus.msg, "Client init failed - reboot to retry", sizeof(tsStatus.msg));
      Log.println("[TS] microlink_init failed.");
      return;
    }
    if (microlink_start(ml) != ESP_OK) {
      // Deliberately not destroyed: microlink_destroy() calls microlink_stop(),
      // which blocks this task for three seconds and cannot reclaim the task
      // stacks anyway. Leaking one instance once beats doing that on a loop.
      tsStatus.failed = true;
      strlcpy(tsStatus.msg, "Client start failed - reboot to retry", sizeof(tsStatus.msg));
      Log.println("[TS] microlink_start failed.");
      return;
    }

    started = true;
    tsStatus.running = true;
    strlcpy(tsStatus.msg, "Connecting", sizeof(tsStatus.msg));
    Log.printf("[TS] Starting as \"%s\"\n", tsCfg.devName);
    return;
  }

  // Polled rather than driven by callbacks: this task already runs at 50 Hz and
  // the state only has to be fresh enough for a once-a-second telemetry frame.
  uint32_t now = millis();
  if (now - lastPollMs < 1000) return;
  lastPollMs = now;

  uint8_t prev = tsStatus.state;
  tsStatus.state = (uint8_t)microlink_get_state(ml);
  tsStatus.peers = (uint8_t)microlink_get_peer_count(ml);
  tsStatus.vpnIp = microlink_get_vpn_ip(ml);

  static const char* const STATE_TXT[] = {
    "Idle", "Waiting for WiFi", "Connecting", "Registering",
    "Connected", "Reconnecting", "Error"
  };
  if (tsStatus.state < sizeof(STATE_TXT) / sizeof(STATE_TXT[0]))
    strlcpy(tsStatus.msg, STATE_TXT[tsStatus.state], sizeof(tsStatus.msg));

  if (tsStatus.state != prev && tsStatus.state == ML_STATE_CONNECTED) {
    char ip[16];
    microlink_ip_to_str(tsStatus.vpnIp, ip);
    Log.printf("[TS] Connected, tailnet address %s\n", ip);
  }
}
