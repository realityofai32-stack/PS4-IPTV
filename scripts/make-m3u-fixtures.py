# Usage: python -I scripts/make-m3u-fixtures.py tests/fixtures/m3u
# Regenerates the sanitized M3U test fixtures (tests/fixtures/m3u). Hosts are example.com/.net/.org or .invalid;
# "exampleuser"/"examplepass" are placeholders. No real provider data.
import os, sys
out = sys.argv[1]
def w(name, text, bom=False, nl="\n"):
    data = text.replace("\n", nl).encode("utf-8")
    if bom:
        data = b"\xef\xbb\xbf" + data
    open(os.path.join(out, name), "wb").write(data)

basic = '''#EXTM3U url-tvg="http://epg.example.com/guide.xml"
#EXTINF:-1 tvg-id="news1.example" tvg-name="News One" tvg-logo="http://img.example.com/logos/news1.png" group-title="News",News One HD
http://stream.example.com/live/news1.ts
#EXTINF:-1 tvg-id="sport1.example" tvg-logo="https://img.example.com/logos/sport1.png" group-title="Sports",Sport 1
http://stream.example.com/live/sport1.m3u8
#EXTINF:-1 tvg-id="trt1.tr" tvg-name="TRT 1" tvg-logo="http://img.example.com/trt1.png" group-title="Türkiye",TR: TRT 1 Çocuk Şarkıları
http://stream.example.com/live/trt1.ts
#EXTINF:-1 tvg-id="perviy.ru" group-title="Россия",Первый канал
http://stream.example.com/live/perviy.ts
#EXTINF:-1 group-title="Ελλάδα",ΕΡΤ1
http://stream.example.com/live/ert1.ts
#EXTINF:-1 group-title="日本",NHK 総合
http://stream.example.com/live/nhk.ts
#EXTINF:-1 group-title="한국",KBS 1TV
http://stream.example.com/live/kbs.ts
#EXTINF:-1 group-title="العربية",الجزيرة
http://stream.example.com/live/aljazeera.ts
'''
w("basic.m3u", basic)
w("basic_crlf.m3u", basic, nl="\r\n")
w("basic_bom_crlf.m3u8", basic, bom=True, nl="\r\n")
w("basic_cr.m3u", basic, nl="\r")

extgrp = '''#EXTM3U
#EXTINF:-1 tvg-id="a",Channel A
#EXTGRP:Music
http://stream.example.com/a.ts
#EXTGRP:Kids
#EXTINF:-1 tvg-id="b",Channel B
http://stream.example.com/b.ts
#EXTINF:-1 group-title="Movies",Channel C
#EXTGRP:Ignored Group
http://stream.example.com/c.ts
#EXTINF:-1 group-title="",Channel D
#EXTGRP:   Docs   
http://stream.example.com/d.ts
#EXTINF:-1,Channel E
http://stream.example.com/e.ts
'''
w("extgrp.m3u", extgrp)

edge = '''

   #EXTM3U   
#EXTINF:-1 tvg-id="x1" group-title="Group 1",  Name, With, Commas  
   http://stream.example.com/1.ts   
#EXTINF:-1 group-title="Group 1",No Logo Channel
http://stream.example.com/2.ts
#EXTINF:-1 tvg-logo="http://img.example.com/3.png",No Group Channel
http://stream.example.com/3.ts
#EXTINF:-1 group-title="Group 1",Duplicate Name
http://stream.example.com/dup-a.ts
#EXTINF:-1 group-title="Group 2",Duplicate Name
http://stream.example.com/dup-b.ts
#EXTINF:-1 group-title="Group 1",Same URL
http://stream.example.com/same.ts
#EXTINF:-1 group-title="Group 2",Same URL
http://stream.example.com/same.ts
#EXTINF:-1 tvg-id="shared.id" group-title="Group 3",Shared Id One
http://stream.example.com/s1.ts
#EXTINF:-1 tvg-id="shared.id" group-title="Group 3",Shared Id Two
http://stream.example.com/s2.ts
#EXTINF:-1 tvg-id="nocomma" tvg-name="Fallback Name" group-title="Group 4"
http://stream.example.com/nocomma.ts
#EXTINF:-1 tvg-id="broken" group-title="Group 4,Unterminated Quote Channel
http://stream.example.com/unterminated.ts
#EXTINF:-1 group-title="Group 4",Missing URL Channel
#EXTINF:-1 group-title='Group 5' tvg-chno=101 tvg-id=unquoted.id,Single Quoted
http://stream.example.com/single.ts
#EXTIMG:http://img.example.com/ignored.png
#EXT-UNKNOWN-DIRECTIVE:value
#KODIPROP:inputstream.adaptive.manifest_type=hls
#EXTVLCOPT:http-referrer=http://www.example.com/
#EXTVLCOPT:http-user-agent=ExamplePlayer/1.0 (Test)
#EXTINF:-1 group-title="Group 5",User Agent Channel
http://stream.example.com/ua.ts
#EXTINF:-1 group-title="Group 5",Invalid URL Channel
not a url at all
#EXTINF:-1 group-title="Group 6",HTTPS Media
https://secure.example.com/live/https.m3u8?token=exampletoken
#EXTINF:-1 group-title="Group 6",RTMP Media
rtmp://media.example.com/live/rtmp
#EXTINF:-1 group-title="Group 6",Credential Path
http://xtream.example.com:8080/exampleuser/examplepass/12345
http://stream.example.com/plain/Plain_Entry.ts
# a plain comment
#EXTINF:-1 group-title="Group 6" ,
http://stream.example.com/unnamed/Unnamed_Stream.ts
#EXTINF:-1 group-title="Group 6",Last Without URL
'''
w("edge_cases.m3u", edge)

hls = '''#EXTM3U
#EXT-X-VERSION:3
#EXT-X-TARGETDURATION:6
#EXT-X-MEDIA-SEQUENCE:100
#EXTINF:6.0,
segment100.ts
#EXTINF:6.0,
segment101.ts
'''
w("hls_media.m3u8", hls)
master = '''#EXTM3U
#EXT-X-STREAM-INF:BANDWIDTH=1280000,RESOLUTION=1280x720
http://stream.example.com/hls/720.m3u8
'''
w("hls_master.m3u8", master)
w("not_playlist.html", "<!DOCTYPE html><html><body><h1>403 Forbidden</h1></body></html>\n")
w("empty.m3u", "\n\n   \n")
open(os.path.join(out, "utf16.m3u"), "wb").write("#EXTM3U\n#EXTINF:-1,A\nhttp://stream.example.com/a.ts\n".encode("utf-16"))
plain = '''http://stream.example.com/one.ts
http://stream.example.com/two.m3u8
'''
w("plain.m3u", plain)
bad = b'#EXTM3U\n#EXTINF:-1 group-title="Bad \xff\xfe Bytes",Bad \xc3 Name\nhttp://stream.example.com/bad.ts\n'
open(os.path.join(out, "invalid_utf8.m3u"), "wb").write(bad)
