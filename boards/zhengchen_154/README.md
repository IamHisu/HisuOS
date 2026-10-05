# Zhengchen 1.54 TFT Wi-Fi

ESP32-S3, 16 MB flash, 8 MB octal PSRAM, 240x240 ST7789 display.

| Peripheral | GPIO |
| --- | --- |
| LCD MOSI / SCLK / CS / DC / RESET / backlight | 41 / 42 / 21 / 40 / 45 / 20 |
| Microphone WS / SCK / DATA | 4 / 5 / 6 |
| Speaker DATA / BCLK / WS | 7 / 15 / 16 |
| BOOT / volume up / volume down | 0 / 10 / 39 |
| Battery ADC / charge status | 8 (ADC1 channel 7) / 9 |

Display settings retain FriendOS: SPI mode 3, 80 MHz, inverted colors and
RGB565 byte swap. The display buffer is internal DMA memory. GPIO20 conflicts
with native USB Serial/JTAG, so console defaults use UART0 and disable that
runtime peripheral. ROM download/recovery is separate from runtime console.

The microphone captures 32-bit left-channel samples at 16 kHz. The speaker
uses I2S1, stereo PCM16 at 16 kHz. Power uses Muse's raw-ADC calibration and
three-reading smoothing; charge status is an estimate, not a calibrated fuel gauge.

## Hardware acceptance

1. Confirm flash/PSRAM size and boot without allocation failures.
2. Confirm backlight, orientation, colors, face animation and GIF playback.
3. Check all buttons and five-second setup action.
4. Configure Wi-Fi and verify saved-network reconnect after reboot.
5. Upload still/GIF assets and verify restoration after reboot.
6. Configure a test AI key through Web Settings and check Vietnamese subtitles.
7. Read mic PCM, play known quiet PCM16 stereo, and verify speaker wiring.
8. Compare battery readings and GPIO9 behavior against actual power states.

Do not enable unattended power actions based on the battery estimate before
completing calibration on the physical unit.
