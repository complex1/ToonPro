"""Blender 5.2 offline conversion for the static Toon Pro pet reference catalog.

Run through prepare_reference3d_pets.py; no Blender code is used by the app.
"""
import bpy,json,math,sys
from pathlib import Path
from mathutils import Vector
arguments=sys.argv[sys.argv.index('--')+1:]
if len(arguments)!=1:raise ValueError('Expected preparation work directory after --')
root=Path(arguments[0]);out=root/'prepared';out.mkdir(parents=True,exist_ok=True)
report=[]
for source,kind,height in [('Dog','dog',.65),('Cat','cat',.38),('Eagle','bird',.55)]:
 bpy.ops.wm.open_mainfile(filepath=str(root/'source-animals/Animal Pack Vol.2 by @Quaternius/Blends'/(source+'.blend')))
 objects=[o for o in bpy.context.scene.objects if o.type in {'MESH','ARMATURE'}];arms=[o for o in objects if o.type=='ARMATURE'];meshes=[o for o in objects if o.type=='MESH']
 for arm in arms:arm.data.pose_position='REST'
 bpy.context.view_layer.update();points=[o.matrix_world@v.co for o in meshes for v in o.data.vertices];low=min(v.z for v in points);high=max(v.z for v in points);scale=height/(high-low)
 normalizer=bpy.data.objects.new('ToonPetPlacement',None);bpy.context.scene.collection.objects.link(normalizer);normalizer.scale=(scale,)*3;normalizer.location.z=-low*scale
 for o in objects:
  if o.parent not in objects:o.parent=normalizer
 for mat in bpy.data.materials:
  color=list(mat.diffuse_color)
  if mat.use_nodes:
   for node in mat.node_tree.nodes:
    if node.type=='BSDF_DIFFUSE':color=list(node.inputs['Color'].default_value);break
  mat.use_nodes=True;mat.node_tree.nodes.clear();shader=mat.node_tree.nodes.new('ShaderNodeBsdfPrincipled');shader.inputs['Base Color'].default_value=color;shader.inputs['Roughness'].default_value=.85;shader.inputs['Metallic'].default_value=0
  output=mat.node_tree.nodes.new('ShaderNodeOutputMaterial');mat.node_tree.links.new(shader.outputs['BSDF'],output.inputs['Surface'])
 bpy.ops.object.select_all(action='DESELECT')
 for o in [normalizer,*objects]:o.select_set(True)
 for arm in arms:arm.data.pose_position='POSE'
 # Export all authored source actions into a single inspectable normalized GLB.
 bpy.context.scene.frame_set(0)
 # Keep anatomical FK nodes; bake existing IK controls into the authored clips.
 if kind in {'dog','cat'}:
  rename={'Bone':'Pelvis','Bone.001':'Spine','Bone.002':'Neck','Bone.003':'Head','Bone.004':'Tail base','Bone.005':'Tail middle','Bone.006':'Tail tip','Bone.007':'Tail end'}
  for first,side,limb in [(8,'Left','hind'),(11,'Right','hind'),(14,'Right','fore'),(17,'Left','fore')]:
   for j,part in enumerate(['upper leg','lower leg','paw']):rename['Bone.%03d'%(first+j)]=side+' '+limb+' '+part
 else:
  rename={'Bone':'Body','Bone.001':'Lower neck','Bone.002':'Upper neck','Bone.003':'Head','Bone.004':'Tail base','Bone.005':'Tail middle','Bone.006':'Tail tip'}
  for first,count,side in [(7,5,'Right'),(12,4,'Left')]:
   for j,part in enumerate(['wing shoulder','wing elbow','wing wrist','primary feathers','wing tip'][:count]):rename['Bone.%03d'%(first+j)]=side+' '+part
  for first,side in [(16,'Right'),(20,'Left')]:
   for j,part in enumerate(['leg','ankle','foot','talons']):rename['Bone.%03d'%(first+j)]=side+' '+part
 for arm in arms:
  for bone in arm.data.bones:
   if bone.name.startswith('IK'):bone.use_deform=False
   elif bone.name in rename:bone.name=rename[bone.name]
 for mesh in meshes:
  for group in mesh.vertex_groups:
   if group.name in rename:group.name=rename[group.name]
 # Old .blend action paths are not always updated when their legacy slots are detached.
 for action in bpy.data.actions:
  for layer in action.layers:
   for strip in layer.strips:
    for bag in strip.channelbags:
     for curve in bag.fcurves:
      for old,new in rename.items():curve.data_path=curve.data_path.replace('pose.bones["'+old+'"]','pose.bones["'+new+'"]')
 # Report the explicit offline four-influence conversion, separately from the
 # runtime importer, which refuses unsupported influence counts.
 deform_names={bone.name for arm in arms for bone in arm.data.bones if bone.use_deform}
 source_weights=[[g.weight for g in vertex.groups if g.weight>0 and mesh.vertex_groups[g.group].name in deform_names] for mesh in meshes for vertex in mesh.data.vertices]
 over_limit=[weights for weights in source_weights if len(weights)>4]
 weight_report={'influenceLimit':4,'maximumSourceDeformInfluences':max(map(len,source_weights),default=0),'verticesReduced':len(over_limit),'maximumDiscardedNormalizedWeight':max((sum(sorted(weights,reverse=True)[4:])/sum(weights) for weights in over_limit),default=0),'method':'Blender glTF exporter keeps four dominant deform weights and renormalizes; runtime importer is unchanged.'}
 normalizer.rotation_euler.z=-math.pi/2 if kind in {'dog','cat'} else math.pi
 bpy.ops.export_scene.gltf(filepath=str(out/(kind+'.glb')),export_format='GLB',use_selection=True,export_animations=True,export_animation_mode='ACTIONS',export_force_sampling=True,export_frame_range=False,export_rest_position_armature=True,export_yup=True,export_cameras=False,export_lights=False,export_skins=True,export_def_bones=True,export_influence_nb=4,export_all_influences=False,export_materials='EXPORT',export_tangents=False)
 report.append({'blenderVersion':bpy.app.version_string,'exporterVersion':'Khronos glTF Blender I/O v5.2.39','kind':kind,'sourceFile':source+'.blend','heightMeters':height,'sourceVertices':sum(len(o.data.vertices) for o in meshes),'maximumSourceVertexInfluences':max((sum(g.weight>0 for g in v.groups) for o in meshes for v in o.data.vertices),default=0),'weightPreparation':weight_report,'sourceBones':sum(len(o.data.bones) for o in arms),'actions':[{'name':a.name,'frames':list(a.frame_range)} for a in bpy.data.actions]})
(root/'export-inventory.json').write_text(json.dumps(report,indent=2))
