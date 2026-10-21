# Gemini connection memory

An existing `sdkconfig` overrides `sdkconfig.defaults`. Updating the defaults or
running an ordinary build does not replace old values. The CMake configuration
now rejects an obsolete network memory profile before producing firmware.

The intended profile prefers PSRAM for ordinary allocations larger than 2 KiB,
reserves 64 KiB for allocations requiring internal/DMA memory, enables PSRAM
allocation for Wi-Fi/LwIP, and uses eight static Wi-Fi TX buffers. Keep TLS in
PSRAM, software AES and the 16 KiB TCP send budget. See `sdkconfig.defaults` for
the exact settings. The reserve is an allocation policy, not a promise that
64 KiB will remain free after internal-only tasks and drivers start.

The October 20 device log repeatedly timed out sending the 24,570-byte Gemini
setup message. At failure, internal free memory was 3,683 or 8,223 bytes, with
largest blocks of 1,472 or 1,536 bytes, while PSRAM still had over 2.5 MB free.
This demonstrates internal memory pressure, not exhaustion of total RAM. It
does not by itself prove that every network timeout is caused by allocation
failure. The log did not show a Wi-Fi disconnect or certificate failure.

After flashing the corrected build, compare `Speaker ready RAM`,
`WSS connected RAM` and `WSS failure RAM` across repeated sessions. Verify setup
completes, microphone turns reach Gemini and music starts after confirmation.
A stable build and configuration checks cannot establish runtime free memory,
network reliability, or audible playback without testing the physical device.
