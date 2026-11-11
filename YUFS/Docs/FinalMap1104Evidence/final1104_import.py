import unreal,json,traceback
from pathlib import Path
out=Path(__file__).with_suffix('.json');result={'textures':[]}
source=Path('C:/Users/ACSLAB4/Desktop/it관_찐최종_화재데이터3종_vdb&bin/vdb')
dest='/Game/Fires/FinalIT1104'
try:
    tools=unreal.AssetToolsHelpers.get_asset_tools()
    for i in (1,2,3):
        name=f'Fire{i}';path=dest+'/'+name
        texture=unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else None
        if not texture:
            task=unreal.AssetImportTask();task.filename=str(source/f'fire_v{i}_0001.vdb')
            task.destination_path=dest;task.destination_name=name;task.automated=True;task.save=True
            task.replace_existing=False;task.factory=unreal.SparseVolumeTextureFactory()
            tools.import_asset_tasks([task])
            objects=task.get_objects();assert len(objects)==1,str(task.imported_object_paths)
            texture=objects[0]
        assert isinstance(texture,unreal.SparseVolumeTexture)
        expected=995 if i==1 else 994
        assert texture.get_num_frames()==expected,str((i,texture.get_num_frames(),expected))
        material=unreal.load_asset(dest+f'/MI_Fire{i}') if unreal.EditorAssetLibrary.does_asset_exist(dest+f'/MI_Fire{i}') else None
        if not material:
            material=tools.create_asset(f'MI_Fire{i}',dest,unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
            unreal.MaterialEditingLibrary.set_material_instance_parent(material,unreal.load_asset('/Engine/EngineMaterials/SparseVolumeMaterial'))
        unreal.MaterialEditingLibrary.set_material_instance_sparse_volume_texture_parameter_value(material,'SparseVolumeTexture',texture)
        assert unreal.MaterialEditingLibrary.get_material_instance_sparse_volume_texture_parameter_value(material,'SparseVolumeTexture')==texture
        for name in unreal.MaterialEditingLibrary.get_static_switch_parameter_names(material):
            if str(name).replace(' ','').lower()=='densitymask':unreal.MaterialEditingLibrary.set_material_instance_static_switch_parameter_value(material,name,True)
        unreal.MaterialEditingLibrary.update_material_instance(material)
        assert unreal.EditorAssetLibrary.save_loaded_asset(material)
        result['textures'].append({'dataset':i,'asset':texture.get_path_name(),'frames':texture.get_num_frames(),'transform':str(texture.get_frame_transform()),'material':material.get_path_name()})
        out.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
    result['complete']=True
except Exception:result['error']=traceback.format_exc()
out.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8');unreal.SystemLibrary.quit_editor()
