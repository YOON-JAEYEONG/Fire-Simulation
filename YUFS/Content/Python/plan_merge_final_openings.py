"""Derive doorway candidates from measured wall sections, without changing the map."""
import json
from pathlib import Path
import unreal
root=Path(unreal.Paths.project_saved_dir())
sections=json.loads((root/'MergeFinalWallSlices.json').read_text(encoding='utf8'))
plan=[]
for z,floor in ((120,20),(470,370)):
    for y,side in ((-8793.75,'North'),(-8553.75,'South')):
        intervals=[]
        for record in sections:
            if record['z']!=z:continue
            for a,b in record['segments']:
                if abs(a[1]-y)<14 and abs(b[1]-y)<14 and abs(a[0]-b[0])>1:
                    intervals.append(sorted((a[0],b[0])))
        merged=[]
        for a,b in sorted(intervals):
            if merged and a<=merged[-1][1]+2:merged[-1][1]=max(b,merged[-1][1])
            else:merged.append([a,b])
        gaps=[[a[1],b[0]] for a,b in zip(merged,merged[1:]) if 90<=b[0]-a[1]<=360]
        for a,b in gaps:
            # Preserve the three stair-hall approaches and the central entrance lobby.
            if a<2745 or b>9190 or (a<6350 and b>5540):continue
            plan.append(dict(floor=floor,side=side,y=y,x0=a,x1=b,width=b-a))
        unreal.log('[MERGE_FINAL_OPENINGS] z=%d side=%s gaps=%s'%(z,side,gaps))
(root/'MergeFinalDoorPlan.json').write_text(json.dumps(plan,indent=2),encoding='utf8')
unreal.log('[MERGE_FINAL_OPENINGS] proposedRoomDoors=%d'%len(plan))
