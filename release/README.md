# TimerRelay — release v0.1-alpha.0

Prebuilt artifacts for the TimerRelay controller (Seeed XIAO ESP32C6 + 6-channel
relay board). Firmware version reported by the device: **0.1-alpha.0**.

Verify downloads against [`SHA256SUMS.txt`](SHA256SUMS.txt):
`sha256sum -c SHA256SUMS.txt`.

## Files

| File | What it is |
|------|------------|
| `timer_relay.bin` | The application image. Use this for **OTA updates** (System tab or `POST /api/ota`). Also the app slice for a manual serial flash. |
| `timer_relay-merged.bin` | All images merged into one file — the easiest **first-time serial flash** (write at `0x0`). |
| `bootloader.bin`, `partition-table.bin`, `ota_data_initial.bin` | The individual pieces for a manual serial flash at their offsets. |
| `russellworks-timerrelay-1.3.0.tgz` | The Bitfocus Companion module (source package). |

## Flashing the firmware (first time — USB serial)

The very first install must be over USB (it writes the bootloader and the OTA
partition layout). Easiest, single-file:

```bash
esptool --chip esp32c6 -b 460800 write-flash 0x0 timer_relay-merged.bin
```

Or flash the pieces individually:

```bash
esptool --chip esp32c6 -b 460800 write-flash \
  0x0     bootloader.bin \
  0x8000  partition-table.bin \
  0x19000 ota_data_initial.bin \
  0x20000 timer_relay.bin
```

On first boot the device joins your configured Wi-Fi, or hosts a `TimerRelay-XXXX`
setup network with a captive portal if none is configured.

## Updating later (over the air — no cable)

Once the device is on your network, update from the web UI's **System** tab, or:

```bash
curl -X POST --data-binary @timer_relay.bin http://timerrelay.local/api/ota
```

The device writes the new image to the spare slot, verifies it, and reboots into
it — rolling back automatically if it fails to start. Settings and Wi-Fi are
preserved across updates.

## Companion module

Install `russellworks-timerrelay-1.3.0.tgz` into Bitfocus Companion. It is the
module source; to produce the official packaged build, unpack it and run
`npm install && npm run build` (needs Node.js). Point the module at the device's
IP/hostname; the REST API is unauthenticated (trusted-LAN).
