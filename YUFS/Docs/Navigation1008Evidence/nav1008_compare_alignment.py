from pathlib import Path
import json, re, struct
root = Path(__file__).parent
before = json.loads((root/'nav1008-alignment-before.json').read_text(encoding='utf-8-sig'))
after = json.loads((root/'nav1008-alignment-after.json').read_text(encoding='utf-8-sig'))
project = Path('C:/Users/ACSLAB4/Documents/Codex/Fire-Navigation-1008/YUFS/Content')
result = {'samples': [], 'raw_byte_checks': 0, 'raw_byte_mismatches': [], 'frame_mismatches': [], 'outside_before': 0, 'outside_after': 0}
for old, new in zip(before['samples'], after['samples']):
    option, frame = new['option'], new['seek']
    assert (old['option'], old['seek']) == (option, frame)
    if int(re.search(r' frame=(-?\d+)', new['diagnostics'])[1]) != frame:
        result['frame_mismatches'].append([option, frame])
    entry = {'option':option, 'frame':frame, 'probes':[]}
    filename = after['settings'][option]['bin']
    with (project/filename).open('rb') as f:
        total, x, y, z = struct.unpack('<4i', f.read(16))
        f.seek(16+frame*x*y*z*2)
        density, heat = f.read(x*y*z), f.read(x*y*z)
    for a,b in zip(old['probes'],new['probes']):
        old_status = re.search(r'eyeStatus=(\w+)', a)[1]
        new_status = re.search(r'eyeStatus=(\w+)', b)[1]
        result['outside_before'] += old_status == 'OutsideDomain'
        result['outside_after'] += new_status == 'OutsideDomain'
        p = {'eye':re.search(r'eye=(.*?) binFrame=',b)[1], 'before_status':old_status, 'after_status':new_status, 'before_smoke':float(re.search(r'eyeSmoke=([\d.]+)', a)[1]), 'after_smoke':float(re.search(r'eyeSmoke=([\d.]+)', b)[1])}
        if new_status == 'Ready':
            idx = int(re.search(r'index=(-?\d+)', b)[1])
            raw = tuple(map(int,re.search(r'rawSmokeHeat=(\d+)/(\d+)',b).groups()))
            expected = (density[idx], heat[idx])
            result['raw_byte_checks'] += 1
            if raw != expected: result['raw_byte_mismatches'].append([option,frame,idx,raw,expected])
            p['index'] = idx
            p['raw'] = raw
        entry['probes'].append(p)
    result['samples'].append(entry)
(root/'nav1008-alignment-comparison.json').write_text(json.dumps(result,indent=2),encoding='utf8')
print({k:v for k,v in result.items() if k!='samples'})
for row in result['samples']:
    if row['option']==2 and row['frame'] in (100,480): print(row)
