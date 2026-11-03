"""Collect existing opt-in real-widget visual fixtures into the UX handoff."""
from pathlib import Path
import json, os, subprocess, re, sys
root=Path(__file__).resolve().parents[1]
out=Path((root/'build/ux-handoff-path.txt').read_text().strip())
items=json.loads((root/'build/ux-preview-inventory.json').read_text())
selected={
 'vector_edit_panel','references','tool_navigation_panel','camera_panel','effects_panel',
 'timeline_edit_panel','audio_panel','drawing_tool_panel','rig_workspace','model_library_panel',
 'rig3d_panel','motion_canvas','scene_workspace','drawing_mode','motion_panel','canvas_feedback',
 'skin3d_panel','video_export_sequence','skin_workspace','advanced_motion_panel','pose_workspace',
 'shots_panel','keyframe_panel','tool_management_panel','audio_export_dialog','pose3d_panel',
 'timeline_panel','brush_management_panel','timeline_improvements','audio_finishing_panel',
 'video_export_dialog','animation_workspace','text_workflow','palette_panel','brush_preset_panel',
 'advanced_brush_tips','brush_surface_pattern','advanced_dynamics','dynamic_stamp_storage'
}
choose=[]
for x in items:
 stem=Path(x['source']).stem.removeprefix('test_').removesuffix('_tests')
 if stem in selected:
  if stem=='brush_surface_pattern':x['methods']=['widgetImportsAtomicallyAndEditsPlane']
  if stem=='drawing_mode':x['methods']=['tabHidesChromeAndRestoresTheArtistsLayout']
  if stem=='timeline_improvements':x['methods']=['rippleDialogCommitsTypedDraftAndSingleUndo','timelineActionsFitNarrowPanelAndRemainDiscoverable']
  if stem=='brush_preset_panel':x['methods']=['responsiveGridRemainsAccessibleAndUsesDelayedHelp','dockIsAvailableFromPanelsAndRestoresAcrossDrawingModeAndReopen']
  if stem=='advanced_dynamics':x['methods']=['dynamicsControlsRetainSettingsWithoutRefreshEdits']
  choose.append(x)
targets=[Path(x['binary']).name for x in choose]
with (root/'build/ux-fixture-build.log').open('w') as f:
 r=subprocess.run(['/opt/homebrew/bin/cmake','--build','build','--target',*targets,'-j6'],cwd=root,stdout=f,stderr=subprocess.STDOUT)
 if r.returncode: print('Fixture build failed; see build/ux-fixture-build.log',flush=True);sys.exit(r.returncode)
records=[]
for x in choose:
 stem=Path(x['source']).stem.removeprefix('test_').removesuffix('_tests')
 destination=out/'supplementary'/stem;destination.mkdir(parents=True,exist_ok=True)
 env=os.environ.copy();env['QT_SCALE_FACTOR']='2'
 for key in ['TOON_UI_PREVIEW','TOON_BRUSH_PRESET_PREVIEW','TOON_BRUSH_TIPS_PREVIEW','TOON_SURFACE_PREVIEW','TOON_ADVANCED_DYNAMICS_PREVIEW','TOON_DYNAMIC_STAMP_PROPERTIES_PREVIEW','TOON_DRAWING_MODE_PREVIEW']:
  env[key]=str(destination)
 platform='cocoa' if stem=='drawing_mode' else 'offscreen'
 if platform=='cocoa':env.pop('QT_SCALE_FACTOR',None)
 log=out/'evidence'/('fixture-'+stem+'.log')
 try:
  with log.open('w') as f:
   p=subprocess.run([str(root/x['binary']),*x['methods'],'-platform',platform],cwd=root,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=90)
  status=p.returncode
 except subprocess.TimeoutExpired:status='timeout'
 files=sorted(str(p.relative_to(out)) for p in destination.glob('*.png'))
 records.append({**x,'platform':platform,'returncode':status,'screenshots':files,'log':str(log.relative_to(out))})
 (out/'fixture-manifest.json').write_text(json.dumps(records,indent=2))
 print(stem, status,len(files),flush=True)
