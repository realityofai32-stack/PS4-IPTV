"""PC-side preflight of the TS / HLS test streams, requested exactly like the PS4 player will.

Requests mirror FFmpeg 5.0's http protocol as configured by mpv 0.34.1 (stream_lavf.c, http.c):
  User-Agent: libmpv | Accept: */* | Range: bytes=0- | Connection: close/keep-alive | Icy-MetaData: 1
Redirects are followed one hop at a time so every hop's scheme/host/status is visible.

It answers, for the PacBrew FFmpeg build (protocols: file ftp http rtmp rtp tcp udp; NO https/tls/crypto):
  - does any hop (stream, playlist, segment) redirect to https?
  - is the HLS encrypted (#EXT-X-KEY, needs the missing "crypto" protocol)?
  - are segment URLs http? which hosts?
  - which codecs are in the MPEG-TS (PAT/PMT stream types)?

Nothing secret is printed: credentials are masked, only scheme/host/port/status/types are shown.
Usage: python -I scripts/preflight-streams.py [config/test_streams.txt]
"""
import http.client
import re
import socket
import sys
import time
from urllib.parse import urljoin, urlsplit

UA = 'libmpv'  # mpv 0.34.1 stream/stream_lavf.c default user-agent
TIMEOUT = 10
MAX_TS_BYTES = 1_500_000
MAX_PLAYLIST_BYTES = 256 * 1024
MAX_REDIRECTS = 8  # FFmpeg http.c MAX_REDIRECTS
SUPPORTED_SCHEMES = {'http'}  # what the PacBrew FFmpeg can open for HLS/TS over the network

STREAM_TYPES = {
    0x01: 'MPEG-1 video', 0x02: 'MPEG-2 video', 0x03: 'MPEG-1 audio (MP2)', 0x04: 'MPEG-2 audio (MP2)',
    0x06: 'PES private (AC3/E-AC3/subtitles/teletext)', 0x0F: 'AAC (ADTS)', 0x11: 'AAC (LATM)',
    0x15: 'ID3 metadata', 0x1B: 'H.264', 0x24: 'HEVC/H.265', 0x81: 'AC3', 0x87: 'E-AC3', 0x86: 'SCTE-35',
}

SECRETS = []


def mask(text):
    for s in sorted(SECRETS, key=len, reverse=True):
        text = text.replace(s, '[REDACTED]')
    text = re.sub(r'(/(?:live|movie|series|timeshift)/)[^/?#\s]+/[^/?#\s]+/', r'\1[REDACTED]/[REDACTED]/', text)
    text = re.sub(r'((?:username|password|user|pass|token|auth)=)[^&\s]+', r'\1[REDACTED]', text, flags=re.I)
    text = re.sub(r'(://)[^/@\s]+@', r'\1[REDACTED]@', text)
    return text


def register_secrets(url):
    m = re.search(r'/(?:live|movie|series|timeshift)/([^/?#]+)/([^/?#]+)/', url)
    if m:
        SECRETS.extend(s for s in m.groups() if len(s) >= 3)
    for m in re.finditer(r'(?:username|password|token)=([^&]+)', url, flags=re.I):
        if len(m.group(1)) >= 3:
            SECRETS.append(m.group(1))


def read_config(path):
    cfg = {}
    with open(path, encoding='utf-8-sig') as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith('#') or '=' not in line:
                continue
            k, v = line.split('=', 1)
            cfg[k.strip().upper()] = v.strip().strip('"\'')
    return cfg


def request(url, keep_alive=False, max_bytes=MAX_TS_BYTES, label=''):
    """GET with FFmpeg-like headers, manual redirects. Returns (final_url, status, headers, body, hops)."""
    hops = []
    for _ in range(MAX_REDIRECTS + 1):
        parts = urlsplit(url)
        hop = {'scheme': parts.scheme, 'host': parts.hostname, 'port': parts.port}
        if parts.scheme not in ('http', 'https'):
            hop['error'] = 'unsupported scheme'
            hops.append(hop)
            return url, None, {}, b'', hops
        conn_cls = http.client.HTTPSConnection if parts.scheme == 'https' else http.client.HTTPConnection
        conn = conn_cls(parts.hostname, parts.port, timeout=TIMEOUT)
        path = parts.path or '/'
        if parts.query:
            path += '?' + parts.query
        t0 = time.time()
        try:
            conn.putrequest('GET', path, skip_host=True, skip_accept_encoding=True)
            conn.putheader('User-Agent', UA)
            conn.putheader('Accept', '*/*')
            conn.putheader('Range', 'bytes=0-')
            conn.putheader('Connection', 'keep-alive' if keep_alive else 'close')
            host = parts.hostname + (f':{parts.port}' if parts.port else '')
            conn.putheader('Host', host)
            conn.putheader('Icy-MetaData', '1')
            conn.endheaders()
            resp = conn.getresponse()
        except (OSError, http.client.HTTPException) as e:
            hop['error'] = f'{type(e).__name__}: {e}'
            hops.append(hop)
            conn.close()
            return url, None, {}, b'', hops
        hop['status'] = resp.status
        hop['ms'] = int((time.time() - t0) * 1000)
        headers = {k.lower(): v for k, v in resp.getheaders()}
        hops.append(hop)
        if resp.status in (301, 302, 303, 307, 308) and 'location' in headers:
            new = urljoin(url, headers['location'])
            register_secrets(new)
            hop['location'] = mask(new)
            conn.close()
            url = new
            continue
        body = b''
        try:
            while len(body) < max_bytes:
                chunk = resp.read(min(65536, max_bytes - len(body)))
                if not chunk:
                    break
                body += chunk
        except (OSError, http.client.HTTPException) as e:
            hop['read_error'] = f'{type(e).__name__}: {e}'
        conn.close()
        return url, resp.status, headers, body, hops
    return url, None, {}, b'', hops + [{'error': 'too many redirects'}]


def show_hops(hops, indent='  '):
    for i, h in enumerate(hops):
        port = f":{h['port']}" if h.get('port') else ''
        line = f"{indent}hop {i}: {h.get('scheme')}://{h.get('host')}{port}"
        if 'status' in h:
            line += f"  HTTP {h['status']} ({h['ms']} ms)"
        if 'location' in h:
            line += f"  -> redirect to {urlsplit(h['location']).scheme}://{urlsplit(h['location']).netloc}"
        if 'error' in h:
            line += f"  ERROR {mask(h['error'])}"
        if 'read_error' in h:
            line += f"  read error {mask(h['read_error'])}"
        if h.get('scheme') not in SUPPORTED_SCHEMES:
            line += '   !! NOT OPENABLE by the PS4 FFmpeg build (no https/tls protocol)'
        print(line)


class BitReader:
    def __init__(self, data):
        self.data, self.pos = data, 0

    def u(self, n):
        v = 0
        for _ in range(n):
            byte = self.data[self.pos >> 3]
            v = (v << 1) | ((byte >> (7 - (self.pos & 7))) & 1)
            self.pos += 1
        return v

    def ue(self):
        zeros = 0
        while self.u(1) == 0:
            zeros += 1
        return (1 << zeros) - 1 + (self.u(zeros) if zeros else 0)

    def se(self):
        v = self.ue()
        return (v + 1) // 2 if v & 1 else -(v // 2)


def parse_h264_sps(nal):
    """Returns a description of an H.264 SPS NAL (without start code), or None."""
    rbsp = bytearray()
    zeros = 0
    for b in nal[1:]:  # remove emulation prevention bytes
        if zeros >= 2 and b == 3:
            zeros = 0
            continue
        rbsp.append(b)
        zeros = zeros + 1 if b == 0 else 0
    try:
        r = BitReader(bytes(rbsp))
        profile = r.u(8)
        r.u(8)
        level = r.u(8)
        r.ue()
        chroma, depth_l, depth_c = 1, 8, 8
        if profile in (100, 110, 122, 244, 44, 83, 86, 118, 128, 138, 139, 134, 135):
            chroma = r.ue()
            if chroma == 3:
                r.u(1)
            depth_l = r.ue() + 8
            depth_c = r.ue() + 8
            r.u(1)
            if r.u(1):  # seq_scaling_matrix_present
                for i in range(8 if chroma != 3 else 12):
                    if r.u(1):
                        last, nxt = 8, 8
                        for _ in range(16 if i < 6 else 64):
                            if nxt:
                                nxt = (last + r.se() + 256) % 256
                            last = nxt or last
        r.ue()  # log2_max_frame_num
        poc_type = r.ue()
        if poc_type == 0:
            r.ue()
        elif poc_type == 1:
            r.u(1)
            r.se()
            r.se()
            for _ in range(r.ue()):
                r.se()
        r.ue()
        r.u(1)
        w_mbs = r.ue() + 1
        h_map = r.ue() + 1
        frame_mbs_only = r.u(1)
        if not frame_mbs_only:
            r.u(1)
        r.u(1)
        crop = (0, 0, 0, 0)
        if r.u(1):
            crop = (r.ue(), r.ue(), r.ue(), r.ue())
        sub_w = 2 if chroma in (1, 2) else 1
        sub_h = 2 if chroma == 1 else 1
        width = w_mbs * 16 - sub_w * (crop[0] + crop[1])
        height = (2 - frame_mbs_only) * h_map * 16 - sub_h * (2 - frame_mbs_only) * (crop[2] + crop[3])
        names = {66: 'Baseline', 77: 'Main', 88: 'Extended', 100: 'High', 110: 'High 10', 122: 'High 4:2:2', 244: 'High 4:4:4'}
        fmt = {0: 'gray', 1: '4:2:0', 2: '4:2:2', 3: '4:4:4'}.get(chroma, '?')
        pix = 'yuv420p' if chroma == 1 and depth_l == 8 else f'{fmt} {depth_l}-bit'
        return (f"H.264 {names.get(profile, profile)} L{level / 10:.1f}  {width}x{height}  {fmt} {depth_l}-bit "
                f"(-> FFmpeg pixel format {pix})  {'progressive' if frame_mbs_only else 'INTERLACED-capable (PAFF/MBAFF)'}")
    except IndexError:
        return None


def pes_payloads(data, start, pid_wanted, limit=400):
    """Concatenated TS payload bytes of one PID (good enough to find NAL units / ADTS headers)."""
    out = bytearray()
    for n in range((len(data) - start) // 188):
        p = data[start + n * 188:start + (n + 1) * 188]
        if p[0] != 0x47 or (((p[1] & 0x1F) << 8) | p[2]) != pid_wanted:
            continue
        afc = (p[3] >> 4) & 3
        off = 4 + (1 + p[4] if afc in (2, 3) else 0)
        if afc in (1, 3) and off < 188:
            out += p[off:]
        if n > limit * 50 and len(out) > 200000:
            break
    return bytes(out)


def describe_video(es):
    for m in re.finditer(rb'\x00\x00\x01([\x27\x47\x67])', es):
        end = es.find(b'\x00\x00\x01', m.end())
        desc = parse_h264_sps(es[m.start() + 3:end if end > 0 else m.start() + 200])
        if desc:
            return desc
    return 'no H.264 SPS found in the sampled data'


def describe_aac(es):
    rates = [96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350]
    for i in range(len(es) - 7):
        if es[i] == 0xFF and (es[i + 1] & 0xF6) == 0xF0:
            prof = ((es[i + 2] >> 6) & 3) + 1
            sr = rates[(es[i + 2] >> 2) & 0xF] if ((es[i + 2] >> 2) & 0xF) < len(rates) else '?'
            ch = ((es[i + 2] & 1) << 2) | (es[i + 3] >> 6)
            return f"AAC ADTS object type {prof} ({'LC' if prof == 2 else 'HE/other'}), {sr} Hz, {ch} channel(s)"
    return 'no ADTS header found in the sampled data'


def analyze_ts(data, indent='  '):
    start = next((i for i in range(min(len(data), 188 * 4))
                  if all(i + k * 188 < len(data) and data[i + k * 188] == 0x47 for k in range(5))), None)
    if start is None:
        print(f'{indent}MPEG-TS: NO sync (0x47 every 188 bytes) in the first bytes -> not plain MPEG-TS')
        return
    packets = (len(data) - start) // 188
    pmt_pids, streams = set(), {}
    for n in range(packets):
        p = data[start + n * 188:start + (n + 1) * 188]
        if p[0] != 0x47:
            continue
        pusi = p[1] & 0x40
        pid = ((p[1] & 0x1F) << 8) | p[2]
        afc = (p[3] >> 4) & 3
        off = 4
        if afc in (2, 3):
            off += 1 + p[4]
        if afc not in (1, 3) or off >= 188 or not pusi:
            continue
        payload = p[off + 1 + p[off]:]  # skip pointer field
        if pid == 0 and len(payload) >= 8:
            sec_len = ((payload[1] & 0x0F) << 8) | payload[2]
            for i in range(8, min(3 + sec_len - 4, len(payload) - 4), 4):
                prog = (payload[i] << 8) | payload[i + 1]
                if prog != 0:
                    pmt_pids.add(((payload[i + 2] & 0x1F) << 8) | payload[i + 3])
        elif pid in pmt_pids and len(payload) >= 12 and payload[0] == 0x02:
            sec_len = ((payload[1] & 0x0F) << 8) | payload[2]
            pil = ((payload[10] & 0x0F) << 8) | payload[11]
            i = 12 + pil
            end = min(3 + sec_len - 4, len(payload))
            while i + 5 <= end:
                st = payload[i]
                epid = ((payload[i + 1] & 0x1F) << 8) | payload[i + 2]
                esl = ((payload[i + 3] & 0x0F) << 8) | payload[i + 4]
                desc = payload[i + 5:i + 5 + esl]
                name = STREAM_TYPES.get(st, f'unknown 0x{st:02X}')
                if st == 0x06:
                    tags, j = set(), 0
                    while j + 2 <= len(desc):  # descriptor loop: tag, length, data
                        tags.add(desc[j])
                        j += 2 + desc[j + 1]
                    if 0x6A in tags:
                        name = 'AC3 (private PES)'
                    elif 0x7A in tags:
                        name = 'E-AC3 (private PES)'
                    elif tags & {0x56, 0x59}:
                        name = 'teletext/subtitles (private PES)'
                streams[epid] = name
                i += 5 + esl
    print(f'{indent}MPEG-TS: sync OK at offset {start}, {packets} packets read, PMT PIDs {sorted(pmt_pids) or "none found"}')
    for pid, name in sorted(streams.items()):
        detail = ''
        if name == 'H.264':
            detail = describe_video(pes_payloads(data, start, pid))
        elif name.startswith('AAC (ADTS)'):
            detail = describe_aac(pes_payloads(data, start, pid))
        print(f'{indent}  PID {pid}: {name}' + (f'  ->  {detail}' if detail else ''))
    if not streams:
        print(f'{indent}  (no PMT parsed in the sampled data)')


def check_ts(url):
    print('== TS stream')
    final, status, headers, body, hops = request(url)
    show_hops(hops)
    if status is None:
        return
    print(f"  final: HTTP {status}  content-type={headers.get('content-type')}  "
          f"content-length={headers.get('content-length', '(none: live)')}  bytes read={len(body)}")
    if status == 206:
        print('  note: server answered the "Range: bytes=0-" request with 206 Partial Content (fine for FFmpeg)')
    if status >= 400:
        print('  !! HTTP error for the exact request FFmpeg sends (User-Agent libmpv, Range, Icy-MetaData)')
        return
    analyze_ts(body)


def check_hls(url):
    print('== HLS playlist')
    final, status, headers, body, hops = request(url, keep_alive=True, max_bytes=MAX_PLAYLIST_BYTES)
    show_hops(hops)
    if status is None or status >= 400:
        if status:
            print(f'  !! HTTP {status}')
        return
    text = body.decode('utf-8', 'replace')
    print(f"  final: HTTP {status}  content-type={headers.get('content-type')}  bytes={len(body)}  "
          f"starts with #EXTM3U: {text.lstrip().startswith('#EXTM3U')}")
    lines = [l.strip() for l in text.splitlines() if l.strip()]
    if any(l.startswith('#EXT-X-STREAM-INF') for l in lines):
        variants = []
        for i, l in enumerate(lines):
            if l.startswith('#EXT-X-STREAM-INF') and i + 1 < len(lines):
                variants.append((l, urljoin(final, lines[i + 1])))
        print(f'  master playlist with {len(variants)} variant(s)')
        for attrs, vurl in variants:
            info = ' '.join(re.findall(r'(BANDWIDTH=\d+|RESOLUTION=\S+?(?=,|$)|CODECS="[^"]*")', attrs))
            print(f'    variant {urlsplit(vurl).scheme}://{urlsplit(vurl).netloc}  {info}')
        if not variants:
            return
        register_secrets(variants[0][1])
        print('== HLS media playlist (first variant)')
        final, status, headers, body, hops = request(variants[0][1], keep_alive=True, max_bytes=MAX_PLAYLIST_BYTES)
        show_hops(hops)
        if status is None or status >= 400:
            return
        lines = [l.strip() for l in body.decode('utf-8', 'replace').splitlines() if l.strip()]
    keys = [l for l in lines if l.startswith('#EXT-X-KEY')]
    segs = [urljoin(final, l) for l in lines if not l.startswith('#')]
    target = next((l.split(':', 1)[1] for l in lines if l.startswith('#EXT-X-TARGETDURATION')), '?')
    maps = [l for l in lines if l.startswith('#EXT-X-MAP')]
    print(f'  media playlist: {len(segs)} segment(s), target duration {target}s, '
          f'live={not any(l.startswith("#EXT-X-ENDLIST") for l in lines)}')
    if keys:
        methods = sorted({(re.search(r'METHOD=([^,]+)', k) or [None, '?'])[1] for k in keys})
        print(f'  !! #EXT-X-KEY present (METHOD={",".join(methods)}): encrypted HLS needs FFmpeg "crypto" protocol, '
              'which the PS4 FFmpeg build does NOT have' if 'NONE' not in methods else
              f'  #EXT-X-KEY METHOD=NONE only (not encrypted)')
    else:
        print('  no #EXT-X-KEY: segments are not encrypted')
    if maps:
        print('  #EXT-X-MAP present: fMP4 segments (mov demuxer, present in the build)')
    schemes = sorted({urlsplit(s).scheme for s in segs})
    hosts = sorted({urlsplit(s).netloc for s in segs})
    print(f'  segment URL schemes: {schemes}  hosts: {[mask(h) for h in hosts]}')
    if any(s not in SUPPORTED_SCHEMES for s in schemes):
        print('  !! some segment URLs are not plain http: the PS4 FFmpeg build cannot open them')
    if segs:
        register_secrets(segs[0])
        print('== HLS first segment')
        _, status, headers, body, hops = request(segs[0], keep_alive=True)
        show_hops(hops)
        if status is not None and status < 400:
            print(f"  final: HTTP {status}  content-type={headers.get('content-type')}  bytes read={len(body)}")
            analyze_ts(body)


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    only = next((a[7:].upper() for a in sys.argv[1:] if a.startswith('--only=')), None)
    path = args[0] if args else 'config/test_streams.txt'
    cfg = read_config(path)
    if only:
        cfg = {k: v for k, v in cfg.items() if k == only}
    for key in ('TS', 'HLS'):
        if key in cfg:
            register_secrets(cfg[key])
    socket.setdefaulttimeout(TIMEOUT)
    print(f'request headers: User-Agent: {UA} | Accept: */* | Range: bytes=0- | Icy-MetaData: 1')
    if cfg.get('TS'):
        print(f"TS  = {mask(cfg['TS'])}")
        check_ts(cfg['TS'])
    else:
        print('TS not set')
    if cfg.get('HLS'):
        print(f"HLS = {mask(cfg['HLS'])}")
        check_hls(cfg['HLS'])
    else:
        print('HLS not set')


if __name__ == '__main__':
    main()
