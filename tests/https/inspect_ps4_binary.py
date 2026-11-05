#!/usr/bin/env python3
"""Proves that the HTTPS implementation is inside the PS4 build, not only in the libraries on disk.

  1. FFmpeg's run-time protocol table (url_protocols in libavformat/protocol_list.c, what avio_enum_protocols and
     ffurl_open walk) of the linked ELF: the entries are R_X86_64_RELATIVE relocations whose addends are the
     protocol structures. https and tls must be among them.
  2. ff_tls_protocol.url_open2 points to tls_open of libavformat/tls_mbedtls.c, which calls the Mbed TLS handshake,
     certificate parsing and verification functions (disassembly).
  3. The libraries' embedded configure line has --enable-mbedtls and https,tls; no host path.
  4. The PKG: pkg_extract, eboot.bin identical to the build's and carrying the TLS code's own strings.

usage: inspect_ps4_binary.py <build dir> <pkg> <work dir>
"""

import hashlib
import os
import re
import subprocess
import sys

REPO = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
LLVM = os.path.join(REPO, "toolchain", "llvm-12.0.1", "bin")
build, pkg, work = sys.argv[1], sys.argv[2], sys.argv[3]
elf = os.path.join(build, "ps4iptv")
fails = 0


def check(ok, what, detail=""):
    global fails
    print("%s  %s%s" % ("PASS" if ok else "FAIL", what, ("  (" + detail + ")") if detail else ""))
    if not ok:
        fails += 1


def tool(name, *args):
    return subprocess.run([os.path.join(LLVM, name), *args], check=True, capture_output=True, text=True).stdout


# symbols: name -> (address, size); address -> names
syms, by_addr = {}, {}
for line in tool("llvm-nm", "-S", "--defined-only", elf).splitlines():
    p = line.split()
    if len(p) == 4:
        addr, size, _, name = int(p[0], 16), int(p[1], 16), p[2], p[3]
        syms.setdefault(name, (addr, size))
        by_addr.setdefault(addr, []).append(name)

relocs = {}
for line in tool("llvm-readelf", "-r", "--wide", elf).splitlines():
    m = re.match(r"\s*([0-9a-f]+)\s+[0-9a-f]+\s+R_X86_64_RELATIVE\s+([0-9a-f]+)", line)
    if m:
        relocs[int(m.group(1), 16)] = int(m.group(2), 16)

# 1. the protocol table
start, size = syms.get("url_protocols", (0, 0))
check(size > 0, "url_protocols table found in the linked ELF", "%d bytes" % size)
table = []
for off in range(start, start + size, 8):
    target = relocs.get(off)
    if target is not None:
        table.append(",".join(n for n in by_addr.get(target, ["?"]) if n.startswith("ff_")) or hex(target))
names = sorted(t.replace("ff_", "").replace("_protocol", "") for t in table)
print("      run-time protocols: " + " ".join(names))
for want in ("https", "tls", "http", "file", "tcp", "udp", "rtmp", "ftp"):
    check(want in names, "protocol %s is in the run-time table" % want)

# 2. tls -> Mbed TLS
tls = syms.get("ff_tls_protocol", (0, 0))[0]
open_fn = relocs.get(tls + 16)   # URLProtocol: name, url_open, url_open2 (tls_mbedtls.c: .url_open2 = tls_open)
fn_names = by_addr.get(open_fn, []) if open_fn else []
check("tls_open" in fn_names, "ff_tls_protocol.url_open2 is tls_open (tls_mbedtls.c)", ",".join(fn_names))
dis = tool("llvm-objdump", "-d", "--no-show-raw-insn", "--disassemble-symbols=tls_open", elf)
for fn in ("mbedtls_ssl_handshake", "mbedtls_x509_crt_parse_file", "mbedtls_ssl_conf_authmode",
           "mbedtls_ssl_set_hostname", "mbedtls_ssl_get_verify_result", "mbedtls_ctr_drbg_seed"):
    check("<%s>" % fn in dis, "tls_open calls %s" % fn)
check("mbedtls_ssl_handshake" in syms, "Mbed TLS handshake linked (defined in the ELF)")

# 3. configure line
strings = subprocess.run([os.path.join(LLVM, "llvm-strings"), elf], check=True, capture_output=True, text=True).stdout
conf = [l for l in strings.splitlines() if "--enable-mbedtls" in l]
check(bool(conf), "FFmpeg configure line embedded with --enable-mbedtls")
check(bool(conf) and "https,tls" in conf[0], "configure line enables https,tls")
check(REPO not in strings, "no build-machine path of this repository in the ELF")

# 4. the PKG
xdir = os.path.join(work, "pkg-extract")
subprocess.run(["rm", "-rf", xdir], check=True)
env = dict(os.environ, LD_LIBRARY_PATH=os.path.join(REPO, "toolchain", "openssl-1.1.1", "lib"),
           DOTNET_ROLL_FORWARD="Major", DOTNET_SYSTEM_GLOBALIZATION_INVARIANT="1")
subprocess.run([os.path.join(REPO, "toolchain", "host-bin", "PkgTool.Core"), "pkg_extract", "--passcode",
                "0" * 32, pkg, xdir], check=True, capture_output=True, env=env)
eboot = os.path.join(xdir, "uroot", "eboot.bin")
sha = lambda f: hashlib.sha256(open(f, "rb").read()).hexdigest()
check(os.path.exists(eboot) and sha(eboot) == sha(os.path.join(build, "eboot.bin")),
      "PKG eboot.bin is the build's eboot.bin", sha(eboot)[:16] if os.path.exists(eboot) else "missing")
data = open(eboot, "rb").read()
for marker in (b"mbedtls_ssl_get_verify_result reported problems", b"mbedtls_x509_crt_parse_file for CA cert",
               b"--enable-mbedtls", b"tls_verify"):
    check(marker in data, "PKG eboot.bin contains %r" % marker.decode())
cacert = os.path.join(xdir, "uroot", "assets", "cacert.pem")
check(os.path.exists(cacert) and sha(cacert) == sha(os.path.join(REPO, "third_party", "cacert", "cacert.pem")),
      "PKG carries the CA bundle (assets/cacert.pem) used for tls-ca-file")
print("%d failure(s)" % fails)
sys.exit(1 if fails else 0)
