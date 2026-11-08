"""Build project-owned procedural VFX materials; no map or fire actor is created."""
import unreal

def material(name, smoke=False):
    path = '/Game/Effects/LocalFire/' + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        asset = unreal.EditorAssetLibrary.load_asset(path)
        unreal.MaterialEditingLibrary.delete_all_material_expressions(asset)
    else:
        asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            name, '/Game/Effects/LocalFire', unreal.Material, unreal.MaterialFactoryNew())
    asset.set_editor_property('blend_mode', unreal.BlendMode.BLEND_TRANSLUCENT)
    asset.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    asset.set_editor_property('two_sided', True)
    lib = unreal.MaterialEditingLibrary
    uv = lib.create_material_expression(asset, unreal.MaterialExpressionTextureCoordinate, -700, 0)
    phase = lib.create_material_expression(asset, unreal.MaterialExpressionScalarParameter, -700, 150)
    phase.set_editor_property('parameter_name', 'Phase')
    power = lib.create_material_expression(asset, unreal.MaterialExpressionScalarParameter, -700, 280)
    power.set_editor_property('parameter_name', 'Strength')
    power.set_editor_property('default_value', 1.0)
    shader = lib.create_material_expression(asset, unreal.MaterialExpressionCustom, -400, 0)
    shader.set_editor_property('output_type', unreal.CustomMaterialOutputType.CMOT_FLOAT4)
    inputs = []
    for name_in in ('UV', 'Phase', 'Strength'):
        item = unreal.CustomInput()
        item.set_editor_property('input_name', name_in)
        inputs.append(item)
    shader.set_editor_property('inputs', inputs)
    if smoke:
        code = '''float2 p=UV*2-1;
float n=0.7+0.15*sin(p.x*13+Phase*1.8)+0.15*sin(p.y*17-Phase*2.1);
p.x+=0.10*sin(p.y*5-Phase); float a=saturate(1-dot(p,p))*n;
return float4(float3(0.10,0.105,0.11),a*0.43*Strength);'''
    else:
        code = '''float h=1-UV.y; float x=(UV.x-0.5)*2;
x+=0.12*sin(h*13-Phase*7)+0.07*sin(h*27-Phase*11);
float width=0.60*(1-h)+0.07;
float edge=saturate((width-abs(x))*9);
float fade=saturate(h*14)*saturate((1-h)*7);
float flicker=0.85+0.15*sin(Phase*13+h*10);
float core=saturate(1-abs(x)/(width*0.65));
float3 color=lerp(float3(2.8,0.10,0.005),float3(5.0,2.1,0.12),core*(1-h));
return float4(color,edge*fade*flicker*Strength);'''
    shader.set_editor_property('code', code)
    for source, name_in in ((uv,'UV'),(phase,'Phase'),(power,'Strength')):
        lib.connect_material_expressions(source, '', shader, name_in)
    rgb = lib.create_material_expression(asset, unreal.MaterialExpressionComponentMask, -100, 0)
    for key, value in [('r',True),('g',True),('b',True),('a',False)]:
        rgb.set_editor_property(key,value)
    alpha = lib.create_material_expression(asset, unreal.MaterialExpressionComponentMask, -100, 140)
    for key, value in [('r',False),('g',False),('b',False),('a',True)]:
        alpha.set_editor_property(key,value)
    assert lib.connect_material_expressions(shader,'',rgb,''), 'RGB connection failed'
    assert lib.connect_material_expressions(shader,'',alpha,''), 'Alpha connection failed'
    lib.connect_material_property(rgb,'',unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    lib.connect_material_property(alpha,'',unreal.MaterialProperty.MP_OPACITY)
    lib.recompile_material(asset)
    unreal.EditorAssetLibrary.save_loaded_asset(asset)
    unreal.log('[LocalFireFX] Created ' + path)

material('M_LocalFlame')
material('M_LocalSmoke', True)
