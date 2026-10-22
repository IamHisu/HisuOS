// Read-only feasibility probe. No login, playback, audio files or firmware changes.
import { createDecipheriv, getCiphers } from 'node:crypto';
const base = 'https://graph.nct.vn';
const headers = {
  'User-Agent': 'HisuOS/0.1',
  'Content-Type': 'application/json',
  'x-os': 'android',
};
const report = { testedAt: new Date().toISOString(), anonymous: true, charts: null, songs: [] };

async function api(path, params = {}) {
  const url = new URL(path, base);
  url.search = new URLSearchParams(params).toString();
  const started = Date.now();
  const response = await fetch(url, { headers, signal: AbortSignal.timeout(15000) });
  const text = await response.text();
  if (text.length > 1024 * 1024) throw new Error('API response exceeds probe limit');
  const json = JSON.parse(text);
  const stats = { http: response.status, code: json.code, bytes: Buffer.byteLength(text), ms: Date.now() - started };
  if (!response.ok || json.code !== 0 || json.success === false)
    throw new Error(JSON.stringify({ ...stats, message: String(json.msg || '').slice(0, 160) }));
  return { data: json.data, stats };
}

function streamUrl(value) {
  try {
    const url = new URL(value);
    const allowed = ['nct.vn', 'nhaccuatui.com', 'nixcdn.com'];
    return url.protocol === 'https:' && !url.username && !url.password &&
      allowed.some(host => url.hostname === host || url.hostname.endsWith('.' + host)) ? url : null;
  } catch { return null; }
}

function mp3Frame(bytes, offset = 0) {
  if (offset + 4 > bytes.length || bytes[offset] !== 0xff || (bytes[offset + 1] & 0xe0) !== 0xe0) return null;
  const version = (bytes[offset + 1] >> 3) & 3;
  const layer = (bytes[offset + 1] >> 1) & 3;
  const bitrateIndex = bytes[offset + 2] >> 4;
  const rateIndex = (bytes[offset + 2] >> 2) & 3;
  if (version === 1 || layer !== 1 || !bitrateIndex || bitrateIndex === 15 || rateIndex === 3) return null;
  const bitrate = (version === 3 ? [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320] :
    [0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160])[bitrateIndex];
  const rate = [44100, 48000, 32000][rateIndex] / (version === 3 ? 1 : version === 2 ? 2 : 4);
  return { bitrateKbps: bitrate, sampleRate: rate, frameBytes: Math.floor((version === 3 ? 144000 : 72000) * bitrate / rate) + ((bytes[offset + 2] >> 1) & 1) };
}

async function sampleStream(value) {
  let url = streamUrl(value);
  if (!url) return { checked: false, reason: 'Not a recognized HTTPS NCT/CDN destination' };
  for (let hop = 0; hop < 4; ++hop) {
    const response = await fetch(url, {
      headers: { Range: 'bytes=0-16383' }, redirect: 'manual', signal: AbortSignal.timeout(15000),
    });
    if ([301, 302, 303, 307, 308].includes(response.status)) {
      const location = response.headers.get('location');
      await response.body?.cancel();
      url = location && streamUrl(new URL(location, url).href);
      if (!url) return { checked: false, reason: 'Rejected redirect' };
      continue;
    }
    const result = {
      checked: true, http: response.status, host: url.hostname,
      contentType: response.headers.get('content-type'), contentRange: response.headers.get('content-range'),
    };
    const reader = response.body?.getReader();
    let bytes = 0;
    const chunks = [];
    if (reader) {
      try {
        while (bytes < 16384) {
          const part = await reader.read();
          if (part.done) break;
          const chunk = Buffer.from(part.value).subarray(0, 16384 - bytes);
          chunks.push(chunk); bytes += chunk.length;
        }
      } finally { await reader.cancel(); }
    }
    const sample = Buffer.concat(chunks);
    result.sampleBytes = bytes;
    result.prefixHex = sample.subarray(0, 12).toString('hex');
    result.id3 = sample.subarray(0, 3).toString('ascii') === 'ID3';
    result.mpegSync = sample.length >= 2 && sample[0] === 0xff && (sample[1] & 0xe0) === 0xe0;
    if (result.id3 && sample.length >= 10 && sample.subarray(6, 10).every(x => x < 128)) {
      const offset = 10 + sample[6] * 2097152 + sample[7] * 16384 + sample[8] * 128 + sample[9] +
        (sample[3] === 4 && (sample[5] & 16) ? 10 : 0);
      result.id3Bytes = offset;
      if (offset <= 2 * 1024 * 1024) {
        const frameResponse = await fetch(url, {
          headers: { Range: `bytes=${offset}-${offset + 2047}` }, redirect: 'manual', signal: AbortSignal.timeout(15000),
        });
        const frameReader = frameResponse.body?.getReader();
        if (frameReader) {
          let frameBytes = Buffer.alloc(0);
          try {
            while (frameBytes.length < 2048) {
              const part = await frameReader.read();
              if (part.done) break;
              frameBytes = Buffer.concat([frameBytes, Buffer.from(part.value).subarray(0, 2048 - frameBytes.length)]);
            }
          } finally { await frameReader.cancel(); }
          const range = frameResponse.headers.get('content-range') || '';
          const first = frameResponse.status === 206 && range.startsWith(`bytes ${offset}-`) ? mp3Frame(frameBytes) : null;
          const second = first && mp3Frame(frameBytes, first.frameBytes);
          result.mp3Frames = first && second && first.sampleRate === second.sampleRate ? { ...first, consecutive: 2 } : null;
        }
      }
    }
    return result;
  }
  return { checked: false, reason: 'Redirect limit' };
}

try {
  const charts = await api('/api/v1/playlist/charts');
  const list = Array.isArray(charts.data) ? charts.data : charts.data?.list || [];
  report.charts = { ...charts.stats, entries: list.slice(0, 5).map(x => ({ key: x.key, name: x.name, title: x.title, period: x.titleDetail })) };
} catch (error) { report.charts = { error: error.message }; }

const queries = process.argv.slice(2);
if (!queries.length) queries.push('ch\u1edd ch\u00fat...', 'LAVIEM', 'L\u01b0u Ni\u00ean');
for (const query of queries) {
  const row = { query };
  report.songs.push(row);
  try {
    const search = await api('/api/v1/search/song', { keyword: query, pageindex: 1, pagesize: 3, correct: 'true' });
    row.search = { ...search.stats, matches: (search.data?.songs || []).map(x => ({ key: x.key, name: x.name, artist: x.artistName })) };
    const match = search.data?.songs?.[0];
    if (!match) continue;
    const detail = await api('/api/v1/song/detail/' + encodeURIComponent(match.key));
    const song = detail.data;
    row.detail = {
      ...detail.stats, key: song.key, name: song.name, artist: song.artistName, duration: song.duration,
      dateFields: Object.fromEntries(Object.entries(song).filter(([k, v]) => /release|publish|date|time/i.test(k) && ['string', 'number'].includes(typeof v))),
      releaseDate: song.dateRelease ? new Intl.DateTimeFormat('vi-VN', { dateStyle: 'short', timeZone: 'Asia/Bangkok' }).format(song.dateRelease) : null,
      streams: (song.streamURL || []).map(x => ({ quality: x.typeUI || x.type, onlyVIP: !!x.onlyVIP, available: !!x.stream })),
    };
    const stream = (song.streamURL || []).find(x => !x.onlyVIP && x.stream && /128/.test(x.typeUI || x.type || '')) ||
      (song.streamURL || []).find(x => !x.onlyVIP && x.stream);
    if (stream) row.audio = await sampleStream(stream.stream);
    const lyric = await api('/api/v2/song/lyric/detail', { songKey: match.key });
    const content = lyric.data?.content || '';
    const timestamps = [...content.matchAll(/\[(\d{1,3}):(\d{2})(?:\.(\d{1,3}))?\]/g)];
    row.lyrics = {
      ...lyric.stats, contentBytes: Buffer.byteLength(content), timedLines: timestamps.length,
      firstTimestamp: timestamps[0]?.[0], lastTimestamp: timestamps.at(-1)?.[0],
      aiGenerated: !!lyric.data?.aiGenerated,
      hasTimedFile: !!lyric.data?.timedLyric, hasProvidedDecodeKey: !!lyric.data?.keyDecryptLyric,
    };
    const timed = streamUrl(lyric.data?.timedLyric);
    if (timed) {
      const timedResponse = await fetch(timed, { redirect: 'error', signal: AbortSignal.timeout(15000) });
      const reader = timedResponse.body?.getReader();
      let bytes = Buffer.alloc(0);
      if (reader) {
        try {
          while (bytes.length <= 65536) {
            const part = await reader.read();
            if (part.done) break;
            bytes = Buffer.concat([bytes, part.value]);
          }
        } finally { await reader.cancel(); }
      }
      const text = bytes.toString('utf8');
      row.lyrics.timedFile = {
        http: timedResponse.status, host: timed.hostname, bytes: bytes.length,
        timedLines: [...text.matchAll(/\[\d{1,3}:\d{2}(?:\.\d{1,3})?\]/g)].length,
        hexEncoded: /^[0-9a-f\s]+$/i.test(text),
      };
      // Use only the key delivered by the anonymous API, never a guessed key.
      const key = lyric.data?.keyDecryptLyric;
      const hex = text.replace(/\s/g, '');
      if (timedResponse.ok && bytes.length <= 65536 && row.lyrics.timedFile.hexEncoded && !(hex.length % 2) && key) {
        if (getCiphers().includes('rc4')) {
          const cipher = createDecipheriv('rc4', Buffer.from(key, 'utf8'), null);
          const decoded = Buffer.concat([cipher.update(Buffer.from(hex, 'hex')), cipher.final()]).toString('utf8');
          const times = [...decoded.matchAll(/\[(\d{1,3}):(\d{2})(?:\.(\d{1,3}))?\]/g)];
          row.lyrics.timedFile.decoded = {
            bytes: Buffer.byteLength(decoded), timedLines: times.length,
            firstTimestamp: times[0]?.[0], lastTimestamp: times.at(-1)?.[0],
          };
        } else row.lyrics.timedFile.decodeNote = 'RC4 unavailable; rerun this diagnostic with --openssl-legacy-provider';
      }
    }
  } catch (error) { row.error = error.message; }
}
console.log(JSON.stringify(report, null, 2));
