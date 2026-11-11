"""Compare traces produced by separate Unreal editor processes on Main."""
import json, math
from pathlib import Path
root=Path(__file__).parent
names=['seed2026_a','seed2026_b','seed2026_30fps','seed2027']
runs={name:json.loads((root/(name+'.json')).read_text()) for name in names}
for name,r in runs.items():
    assert r['complete'] and len(r['samples'])==20,(name,r.get('error'))
    assert max(abs(s['time']-s['second']) for s in r['samples'])<.002, name+' sample clock mismatch'
    assert any(math.dist(n['position'],r['initial'][i]['position'])>100 for s in r['samples'] for i,n in enumerate(s['npcs'])), name+' did not walk'
assert len({r['pid'] for r in runs.values()})==len(runs),'Processes were not distinct'
def compare(a,b):
    pos=rot=velocity=0.
    for x,y in zip(a['samples'],b['samples']):
        for n,m in zip(x['npcs'],y['npcs']):
            assert n['id']==m['id']
            pos=max(pos,math.dist(n['position'],m['position']))
            rot=max(rot,max(abs((v-w+180)%360-180) for v,w in zip(n['rotation'],m['rotation'])))
            velocity=max(velocity,math.dist(n['velocity'],m['velocity']))
    return {'max_position_difference_cm':pos,'max_rotation_difference_degrees':rot,'max_velocity_difference_cm_s':velocity}
result={'map':'/Game/Maps/Main','npc_count':4,'duration_seconds':20,'headless_PIE':True,
        'distinct_process_ids':{k:v['pid'] for k,v in runs.items()},
        'same_seed_restart':compare(runs[names[0]],runs[names[1]]),
        'same_seed_30_vs_60_fps':compare(runs[names[0]],runs[names[2]]),
        'changed_seed':compare(runs[names[0]],runs[names[3]])}
result['passed']=(result['same_seed_restart']['max_position_difference_cm']<.01 and result['same_seed_30_vs_60_fps']['max_position_difference_cm']<.01 and result['changed_seed']['max_position_difference_cm']>100)
(root/'comparison.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result,indent=2))
