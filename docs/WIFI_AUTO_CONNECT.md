# Wi-Fi Auto Connect

Reviewed against HisuOS commit `46cf291` on 2026-10-13.

When Wi-Fi is enabled, including at boot, the worker scans first. It tries
supported visible saved SSIDs by signal strength, then saved SSIDs absent from
the scan, including hidden networks. Protected credentials retain the
WPA2-or-better auth threshold. Visible open lookalikes are rejected.

Manual scans also auto-connect when idle. Scanning while connected does not
roam or replace the active connection. Scanning during an existing connection
attempt preserves that attempt and its existing retry delay. Repeated enable
commands do not interrupt an already enabled station.

Auto-connect rejects password/security mismatches, especially an open network
using the name of a saved password-protected SSID. It never automatically uses
credentials from a failed provisioning attempt or saves credentials merely
because an SSID was discovered. Credentials are saved only after GOT_IP.

Failure history is per SSID and lives in RAM. Auth rejection blocks automatic
selection until a successful explicit connection or reboot; other connection
failures impose a 60-second cooldown. After an automatic attempt fails auth or
exhausts retries, another scan can select a backup. Idle disconnected stations
rescan every 30 seconds. Scans preserve `password_rejected`, which both the menu
and web use to offer password correction. Success clears the per-SSID failure.

An Internet worker probes after GOT_IP and every 30 seconds, using HTTPS Google
204 and an HTTP Microsoft exact-body fallback without following redirects.
Connection generations discard stale results. The UI distinguishes checking,
online, captive portal and offline. Probe success does not guarantee access to
an AI provider.

Auto-selected networks can roam after two failed Internet checks, imposing a
120-second cooldown on the old network. Explicitly selected networks stay
connected. Before roaming, scan again while retaining the current connection.
Only a visible saved SSID from that successful scan, with supported security,
matching credentials and no active failure block, can replace the current link.
Connect directly to that selected candidate without scanning again after
disconnecting. If scanning fails or no eligible alternative is seen, preserve
the current connection and its Internet warning. Absent or hidden saved SSIDs
remain fallback candidates only while disconnected. A discovered router may
still disappear or reject a connection; radio behavior needs device validation.

On 2026-10-13, network host tests passed for fallback, hidden networks, auth
history, cooldowns, probes, stale results and offline roaming. UI/Wi-Fi/power,
calendar, media model and DNS tests also passed. Wi-Fi browser tests passed at
1280x800, 390x844 and 320x720, including password correction after rescan and
Internet warning recovery. HTTP and radio APIs are mocked.

Device validation remains unconfirmed in this chat. Prioritize a wrong-password
strongest AP with a usable backup, hidden SSID after reboot, password correction
after scan, router off/on, station toggling during Setting and Internet loss
when the only saved alternative is out of range. Host tests do not validate
real radio events, DHCP, NVS persistence or AP+STA channel changes.
