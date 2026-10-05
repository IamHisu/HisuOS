# Reused code

- FriendOS source, Vietnamese font and bundled web assets were copied from the
  local project. Preserve the notices already present in those files. The local
  FriendOS root had no LICENSE file; this migration does not invent one.
- Speaker setup, battery calibration and microphone sample conversion are
  adapted from Meta's muse-gadget-sdk Zhengchen board code, under Apache 2.0.
  The full upstream license is included as `Muse-Apache-2.0.txt`.
- `web/assets/gif-resize.js` retains the bundled library legal comments.
- ESP-IDF, LVGL, button and their transitive dependencies retain their upstream
  licenses through their packages. They are resolved by Component Manager.
