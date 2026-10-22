# NCT Source Feasibility Probe

Follow-up: the firmware provider has now migrated to NCT; see `music.md` for
the current adapter and board test checklist. The measurements below describe
the earlier read-only PC feasibility probe, not a physical board playback test.

Tested 2026-10-22, approximately 13:10 Asia/Bangkok, on the development PC.
No firmware provider was changed, no board/COM was accessed, and no sound was
played. No NCT account, user token, payment or VIP stream was used.

## Actual Requests

- API origin: `https://graph.nct.vn`.
- Search: `/api/v1/search/song` with keyword, pageindex=1, pagesize=3.
- Detail: `/api/v1/song/detail/<key>`.
- Lyrics: `/api/v2/song/lyric/detail?songKey=<key>`.
- Charts: `/api/v1/playlist/charts`.
- HTTP 200 / API code 0 on tested metadata requests.
- Chart index included Top 50 Nhac Viet marked updated 22/10/2026 and trending
  chart marked updated 21/10/2026. A chart update is not a song release date.

Endpoints were inspected in the author's current client source, not executed
as an installed dependency:
https://github.com/carlylekatto/nct.js/tree/main/src

## Results

Release dates below are the service's `dateRelease` metadata, converted to UTC+7;
they have not been independently verified with artist announcements.

| Track | NCT key | Release date | Anonymous audio | Timed lyric |
| --- | --- | --- | --- | --- |
| Khong Mon Dang Ho Doi (Rumba Ver) - Nguyen Vi | x5DG3gBpOCYu | 2026-10-08 | MP3 128 kbps / 44,100 Hz | Absent; plain text only |
| cho chut... - TINH HA SAY HI, CONGB | 5pylA87JeFm5 | 2026-09-19 | MP3 128 kbps / 44,100 Hz | 69 markers, 00:04.29 to 03:20.78 |
| LAVIEM - TINH HA SAY HI and collaborators | cdDJjfkc8OFC | 2026-09-05 | MP3 128 kbps / 44,100 Hz | 134 markers, 00:02.88 to 04:24.42 |
| Tim Em - Hngle, Bao Anh | m4OopfbooS9C | 2026-06-18 | MP3 128 kbps / 44,100 Hz | 87 markers, 00:33.28 to 04:27.19 |

Audio checks used HTTP Range and read only small byte samples, never a whole
song. CDN returned HTTP 206, `audio/mpeg`, then two consecutive valid Layer III
frames after ID3. This verifies accessible MP3 data, not decoded/audible playback
or sustained throughput on ESP32. Both `stream.nct.vn` and `a01.nct.vn` occurred.

## Integration Implications

1. Public 128 kbps streams were available without login for these tracks. Do
   not select entries marked `onlyVIP` or assume all catalog entries are free.
2. The API `content` field was plain text, but `timedLyric` sometimes pointed
   to a separate hexadecimal-encoded LRC file on `lyric.nct.vn`. Using only
   `content` incorrectly concludes that every track lacks timing.
3. The anonymous lyric API itself supplied `keyDecryptLyric`; the diagnostic
   used that provided key with the runtime's RC4 implementation. No guessed
   key, account token or lyric text was logged. Decoded files were 2.7-5.6 KiB.
4. Missing timed lyrics must remain a normal fallback. The new Rumba version
   has plain lyrics but no timed file; firmware must not invent synchronization.
5. ID3 artwork/metadata is large: approximately 241,780-241,906 bytes before
   the first MP3 frame on the tested tracks. A board implementation should
   skip it incrementally without buffering the whole tag in internal RAM.
6. Search returned about 7.8-10.4 KiB, detail about 2.4-4.1 KiB. The charts
   index was about 116-117 KiB; it does not fit the existing music 64 KiB JSON
   limit and is unnecessary for ordinary name-based search/play.
7. Keep stream/lyric URL allowlists, TLS certificate validation, cancellation,
   PSRAM allocation and same-track lyric ownership. Drive lyric position from
   audio actually played, not elapsed time since network download started.
8. These tests do not establish API stability, supported third-party use or
   permission to redistribute music/lyrics. They do not guarantee every new
   release is present or freely playable.

## Reproduction

From HisuOS, using Node.js built-ins only:

```powershell
node --openssl-legacy-provider tools/probe-nct.mjs 'Khong Mon Dang Ho Doi (Rumba Ver)' 'LAVIEM'
```

The legacy-provider switch is confined to this PC diagnostic process, allowing
the runtime's RC4 implementation for the API-provided lyric format. It does
not change TLS verification or board crypto settings. JSON output contains
metadata, byte counts and timestamps, never full lyrics or signed stream URLs.

Conclusion: switching music providers and adding timed captions is technically
plausible with direct Wi-Fi access. Implement and test the adapter on the board
before replacing the working Zing source. Firmware remains unchanged here.
