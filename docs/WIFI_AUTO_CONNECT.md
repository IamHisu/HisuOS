# Wi-Fi Auto Connect

When Wi-Fi is enabled, including at boot if the saved Wi-Fi switch is on,
the network worker scans first. When disconnected and not resuming an explicit
join, it selects the strongest supported saved SSID in the scan results and
loads its stored password. A successful scan with no eligible saved network
leaves the station disconnected and the results available for manual selection.

Manual scans also auto-connect when idle. Scanning while connected does not
roam or replace the active connection. Scanning during an existing connection
attempt preserves that attempt and its existing retry delay. Repeated enable
commands do not interrupt an already enabled station.

Auto-connect rejects password/security mismatches, especially an open network
using the name of a saved password-protected SSID. It never automatically uses
credentials from a failed provisioning attempt or saves credentials merely
because an SSID was discovered. Existing bounded retry and authentication-error
handling remain in effect; there is no periodic background rescan or automatic
fallback to another SSID after authentication fails. Hidden SSIDs are not found
by this scan and still require manual connection.

Host tests cover saved/unsaved, supported/unsupported, security mismatches and
invalid passwords. Actual scan/connect behavior still needs board verification:
toggle off/on near a saved AP, repeat after reboot, try multiple saved APs,
scan while connected, and enable Wi-Fi without any saved AP in range.
