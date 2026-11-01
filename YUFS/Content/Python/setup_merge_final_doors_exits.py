"""Add interactive doors and visible exits to merge_final's actual Main_level.

Room openings are derived from the imported wall sections. No building mesh or
NPC decision code is replaced. Re-running replaces only MF_ prefixed actors.
"""
import json,math
from pathlib import Path
import unreal

MAP='/Game/Maps/it관_찐최종/Main_level'
PREFIX='MF_'
level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
assert level.load_level(MAP)
plan=json.loads(Path(__file__).with_name('MergeFinalDoorPlan.json').read_text(encoding='utf8'))
for actor in actors.get_all_level_actors():
    if actor.get_actor_label().startswith(PREFIX):actors.destroy_actor(actor)

folder='/Game/YUFS/MergeFinal'
unreal.EditorAssetLibrary.make_directory(folder)
base=unreal.load_asset('/Engine/BasicShapes/BasicShapeMaterial')
def material(name,color):
    path=folder+'/'+name
    mat=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else unreal.AssetToolsHelpers.get_asset_tools().create_asset(name,folder,unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
    unreal.MaterialEditingLibrary.set_material_instance_parent(mat,base)
    unreal.MaterialEditingLibrary.set_material_instance_vector_parameter_value(mat,'Color',unreal.LinearColor(*color))
    unreal.EditorAssetLibrary.save_loaded_asset(mat)
    return mat
green=material('MI_ExitGreen',(0.015,0.55,0.12,1))
dark=material('MI_ExitFrame',(0.03,0.06,0.07,1))
concrete=material('MI_ExteriorApron',(0.26,0.28,0.30,1))
cube=unreal.load_asset('/Engine/BasicShapes/Cube')
def box(label,position,size,mat,collision=False,yaw=0):
    actor=actors.spawn_actor_from_class(unreal.StaticMeshActor,unreal.Vector(*position),unreal.Rotator(0,0,yaw))
    actor.set_actor_label(PREFIX+label);actor.set_folder_path('MergeFinal/Exits')
    mesh=actor.static_mesh_component;mesh.set_static_mesh(cube);mesh.set_material(0,mat)
    mesh.set_collision_profile_name('BlockAll' if collision else 'NoCollision')
    actor.set_actor_scale3d(unreal.Vector(*(v/100 for v in size)))
    return actor

door_class=unreal.load_class(None,'/Script/YUFS.YUFSInteractionDoor')
for i,item in enumerate(plan):
    # Native actor's jambs extend 6% beyond each end of its +Y leaf.
    width=item['width']/1.12
    hinge=item['x0']+width*.06
    door=actors.spawn_actor_from_class(door_class,unreal.Vector(hinge,item['y'],item['floor']),unreal.Rotator(0,0,-90))
    door.set_actor_label('%sDoor_%s_%s_%02d'%(PREFIX,'1F' if item['floor']<100 else '2F',item['side'],i+1))
    door.set_folder_path('MergeFinal/InteractiveDoors')
    door.set_actor_scale3d(unreal.Vector(1,width/100,1))
    door.set_editor_property('OpenSeconds',1.2)
    door.tags=['MergeFinalDoor','WallSectionDerived']

exit_class=unreal.load_class(None,'/Script/YUFS.YUFSExitPoint')
# Floor z=20 is measured from the final imported slabs. +X of each marker faces out.
exits=[('West',(2403.824,-8673.75,20),180,200),
       ('East',(9593.824,-8673.75,20),0,200),
       ('Main',(5983.824,-7283.75,20),90,500)]
for name,p,yaw,width in exits:
    angle=math.radians(yaw);forward=unreal.Vector(math.cos(angle),math.sin(angle),0)
    origin=unreal.Vector(*p)
    exit_actor=actors.spawn_actor_from_class(exit_class,origin+forward*200,unreal.Rotator(0,0,yaw))
    exit_actor.set_actor_label(PREFIX+'EmergencyExit_'+name);exit_actor.set_folder_path('MergeFinal/Exits')
    exit_actor.set_editor_property('ExitID','Final_'+name)
    exit_actor.set_editor_property('ExitWidth',float(width))
    exit_actor.set_editor_property('bIsFamiliarEntry',name=='Main')
    exit_actor.tags=['MergeFinalExit','ExistingOpening']
    pad=origin+forward*250-unreal.Vector(0,0,10)
    box(name+'_ExteriorApron',(pad.x,pad.y,pad.z),(600,650,20),concrete,True,yaw)
    for side in (-1,1):
        local=unreal.Vector(0,side*(width/2+5),110)
        pos=origin+forward*local.x+unreal.Vector(-forward.y,forward.x,0)*local.y+unreal.Vector(0,0,110)
        box(name+'_Frame_'+str(side),(pos.x,pos.y,pos.z),(12,10,220),dark,False,yaw)
    box(name+'_Lintel',(p[0],p[1],p[2]+225),(12,width+20,10),dark,False,yaw)
    box(name+'_ExitSign',(p[0],p[1],p[2]+258),(8,120,40),green,False,yaw)
    # Two-sided readable signage, mounted to the opening rather than the terminal point.
    for face in (-1,1):
        at=origin+forward*face*5+unreal.Vector(0,0,246)
        text=actors.spawn_actor_from_class(unreal.TextRenderActor,at,unreal.Rotator(0,0,yaw if face>0 else yaw+180))
        text.set_actor_label(PREFIX+name+'_EXIT_Text_'+str(face));text.set_folder_path('MergeFinal/Exits')
        text.text_render.set_text('EXIT')
        text.text_render.set_world_size(27)
        text.text_render.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)
        text.text_render.set_text_render_color(unreal.Color(255,255,255,255))

all_actors=actors.get_all_level_actors()
for actor in all_actors:
    if actor.get_class().get_name()=='NavMeshBoundsVolume':
        # Enclose the real building, its three external aprons and both storeys.
        old_center,old_extent=actor.get_actor_bounds(False)
        scale=actor.get_actor_scale3d()
        actor.set_actor_location(unreal.Vector(6000,-8650,400),False,False)
        actor.set_actor_scale3d(unreal.Vector(scale.x*4400/old_extent.x,scale.y*2000/old_extent.y,scale.z*1000/old_extent.z))
    if actor.get_class().get_name()=='PlayerStart':
        location=unreal.Vector(6000,-13200,5500);target=unreal.Vector(6000,-8650,100)
        actor.set_actor_location(location,False,False)
        actor.set_actor_rotation(unreal.MathLibrary.find_look_at_rotation(location,target),False)
level.save_current_level()
unreal.log('[MERGE_FINAL_SETUP] map=%s interactiveDoors=%d exits=3 saved=True'%(MAP,len(plan)))
