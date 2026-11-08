import json,re,collections
from pathlib import Path
root=Path(__file__).parent
summary={'conditions':'Two rendered Main PIE cases; 20 NPCs, same non-overlapping projected placements and transient alarm-responsive configs; paused BIN/VDB frame0 isolates traffic; no actor movement overrides after spawn.', 'cases':[]}
data={key:json.loads((root/f'nav1011-crowd-{key}.json').read_text(encoding='utf-8-sig')) for key in ('before','after')}
for key,r in data.items():
 assert not r.get('error'),r.get('error')
 assert len(r['runs'])==2 and all(a['count']==20 and a['samples'][-1]['t']==58 for a in r['runs'])
for case in (0,1):
 coordinates=lambda p:tuple(float(v) for v in re.findall(r'[xyz]: ([-0-9.]+)',p))
 row={'case':case,'placements_match':[coordinates(p) for p in data['before']['runs'][case]['positions']]==[coordinates(p) for p in data['after']['runs'][case]['positions']]}
 for key in ('before','after'):
  r=data[key]['runs'][case]
  counts=collections.Counter(n['traffic'] for s in r['samples'] for n in s['npcs'])
  finish=next((s['t'] for s in r['samples'] if all('tick=0' in n['audit'] for n in s['npcs'])),None)
  waiting=sum(n['traffic'].find('YIELDING')>=0 for s in r['samples'] for n in s['npcs'])
  bad_frames=sum('frame=0 ' not in s['diagnostics'] for s in r['samples'])
  row[key]={'finish_seconds_first_sample':finish,'resolved_at_58':sum('tick=0' in n['audit'] for n in r['samples'][-1]['npcs']), 'yielding_person_seconds_samples':waiting,'traffic_samples':dict(counts),'wrong_frame_samples':bad_frames}
 summary['cases'].append(row)
(root/'nav1011-comparison.json').write_text(json.dumps(summary,indent=2),encoding='utf8')
print(json.dumps(summary,indent=2))
