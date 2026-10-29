from pathlib import Path
import json, re, math
root=Path(__file__).parent
data=json.loads((root/'nav1008_live_probe.json').read_text(encoding='utf8'))
assert not data.get('error'), data.get('error')
assert len(data['runs'])==3
summary={'runs':[], 'frame_mismatches':[], 'missing_or_invalid_live_samples':[]}
for run in data['runs']:
    assert run['samples'][-1]['t']==25
    r={'option':run['option'], 'npc_count':run['count'], 'seconds':25, 'max_self_smoke':0, 'max_raw_heat':0, 'states':[], 'last_frame':None}
    states=set()
    for sample in run['samples']:
        frame=int(re.search(r' frame=(-?\d+)',sample['diagnostics'])[1])
        r['last_frame']=frame
        if frame!=math.floor(sample['visual_frame']): summary['frame_mismatches'].append([run['option'],sample['t'],frame,sample['visual_frame']])
        for audit in sample['npcs']:
            states.add(re.search(r' state=(\w+)',audit)[1])
            r['max_self_smoke']=max(r['max_self_smoke'],float(re.search(r' self=([\d.]+)',audit)[1]))
            m=re.search(r'rawSmokeHeat=(\d+)/(\d+)',audit)
            if m: r['max_raw_heat']=max(r['max_raw_heat'],int(m[2]))
            if 'eyeStatus=MissingData' in audit or 'eyeStatus=InvalidMapping' in audit: summary['missing_or_invalid_live_samples'].append([run['option'],sample['t'],audit])
    r['states']=sorted(states)
    summary['runs'].append(r)
(root/'nav1008-live-summary.json').write_text(json.dumps(summary,indent=2),encoding='utf8')
print(json.dumps(summary,indent=2))
