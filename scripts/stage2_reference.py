# Usage: python3 scripts/stage2_reference.py unit_tests/data/stage2/stream.pcap unit_tests/data/stage2
# Independent Python reference: expected PDFs and per-frame routes for the Stage 2 capture (brief's rules).
import struct, sys, collections, pathlib
pcap = pathlib.Path(sys.argv[1]).read_bytes(); out = pathlib.Path(sys.argv[2])
recs = []; pos = 24
while pos + 16 <= len(pcap):
    n = struct.unpack('>I', pcap[pos+8:pos+12])[0]; recs.append(pcap[pos+16:pos+16+n]); pos += 16 + n
VM = {bytes.fromhex(x) for x in ('000c29','005056','001c14')}
MON = {bytes.fromhex(x) for x in ('001aa0','001ec9','00155d')}
files = collections.defaultdict(dict); routes = []
for r in recs:
    if r[:4] == b'CTFS' and r[13:16] == b'\0\0\0':
        seq, ln = struct.unpack('>HH', r[4:8]); files[r[8]].setdefault(seq, r[16:16+ln]); continue
    m = 0
    if r[6:9] not in VM:
        m |= {6: 1, 17: 4}.get(r[23], 0)
    if r[0:3] in MON: m |= 2
    routes.append(str(m))
(out / 'routes.txt').write_text('\n'.join(routes) + '\n')
for fid, segs in files.items():
    (out / f'{fid}.pdf').write_bytes(b''.join(segs[s] for s in sorted(segs)))
print(len(recs), 'records,', len(routes), 'frames,', collections.Counter(routes))
