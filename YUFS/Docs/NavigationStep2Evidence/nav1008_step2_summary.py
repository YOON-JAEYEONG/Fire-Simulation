from pathlib import Path
import json,re,datetime,collections
root=Path(__file__).parent
live=json.loads((root/'nav1008_step2_live_probe.json').read_text(encoding='utf-8-sig'))
assert not live.get('error'),live.get('error')
assert len(live['runs'])==3 and all(r['samples'][-1]['t']==54 for r in live['runs'])
summary={'normal_runs':[], 'query_comparison_first_25_seconds':{}, 'detours':[], 'limits':'Same scripted placements, playback and seeds; exposure scale now 3. These are not broad escape statistics.'}
for r in live['runs']:
    waiting={}
    events={}
    for s in r['samples']:
        for a,animation in zip(s['npcs'],s['animations']):
            name=re.search(r'npc=(\S+)',a)[1]
            state=re.search(r' state=(\w+)',a)[1]
            if 'waitingSafe=1' in a: waiting[name]={'request':int(re.search(r' request=(\d+)',a)[1]), 'animation':animation['active'], 't':s['t']}
            if state in ('Crawling','Incapacitated'):
                events.setdefault(name,{}) .setdefault(state,s['t'])
    summary['normal_runs'].append({'option':r['option'],'npc_count':r['count'],'last_waiting':waiting,'exposure_states':events,'configs':r['exposure_configs']})
for label,file in [('before','nav1008-live-after.log'),('after','nav1008-step2-live.log')]:
    runs=[];start=None;index=-1
    for line in (root/file).read_text(encoding='utf-8-sig',errors='replace').splitlines():
        stamp=re.match(r'\[([^]]+)\]',line)
        if not stamp:continue
        try:t=datetime.datetime.strptime(stamp[1],'%Y.%m.%d-%H.%M.%S:%f')
        except ValueError:continue
        if 'Phase: FireActive' in line:
            start=t;index+=1;runs.append({'option':index,'pathfinding':collections.Counter(),'unsafe_failures':collections.Counter(),'exits':0})
        if start is None:continue
        dt=(t-start).total_seconds()
        if 'selected safe local detour' in line and label=='after':summary['detours'].append({'option':index,'seconds':dt,'log':line})
        if dt<0 or dt>25:continue
        agent=re.search(r'agent=(\S+)',line)
        if agent and 'status=Pathfinding' in line:runs[index]['pathfinding'][agent[1]]+=1
        if agent and 'status=Failed' in line and 'failure=UnsafePath' in line:runs[index]['unsafe_failures'][agent[1]]+=1
        if 'evacuated successfully' in line:runs[index]['exits']+=1
    summary['query_comparison_first_25_seconds'][label]=runs
(root/'nav1008-step2-summary.json').write_text(json.dumps(summary,indent=2),encoding='utf8')
print(json.dumps(summary,indent=2))
