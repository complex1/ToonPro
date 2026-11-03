"""Build a portable designer handoff from verified real-widget captures."""
from pathlib import Path
from collections import defaultdict, Counter
import csv, hashlib, html, json, re, shutil
from PIL import Image, ImageDraw, ImageOps
ROOT=Path(__file__).resolve().parents[1]
OUT=Path((ROOT/'build/ux-handoff-path.txt').read_text().strip())
E=html.escape
# Descriptions concern shipped behavior. Captured controls below supply precise names/options.
ABOUT={
'Workspace':('The movie/shot drawing workspace, including the welcome state, working canvas and Tab drawing mode.','The artwork frame sits inside a production sheet with margins and a metadata header. Docks resize, float and tab; Toolbox is a fixed icon column. Tab hides working panels for drawing and returns to the previous layout. Quick access retains pinned tools, colors and navigation. Zoom, view rotation and AA are display preferences; they do not edit artwork.'),
'Toolbars':('Project commands, customizable quick access and the Toolbox icon column.','Project controls expose New, Open, Save, artwork Undo/Redo, project setup, fit view, canvas AA and the drawing workspace preset. Quick access pins tools, color swatches and navigation. Toolbox entries can be groups or individual tools. Choosing a group populates Tools; a standalone entry supplies a single tool.'),
'Quick Access':('A customizable strip of frequent tools, colors and canvas navigation.','The add menu offers tools, a color and navigation. Each item activates its tool, sets the drawing color or navigates the view. Items can be reordered/removed. The drawing mode button mirrors Tab. Preserve recognition and pen hit areas when reducing visual size.'),
'Tools':('Choose a drawing tool or brush from the active Toolbox group.','Tag and search filters share a row and can be cleared without changing the active drawing tool. Tools can display names with previews or an icon grid. Plus opens Add Tools; menus provide duplicate, rename, remove and management/import commands. Brush previews show their marks; organizational colors are separate from paint colors.'),
'Brush Preset':('A brush-size preset grid, independent of the brush library.','Each preset selects a brush size; the active size has a distinct state. The add action creates a custom size. Presets support organization/removal through their available menus. This is a size selector, not a browser for different brush engines.'),
'Layers':('Manage the drawing stack and active artwork target.','Top controls set blend mode, opacity and locks/clipping. Rows contain visibility, a checkerboard content thumbnail, type indicators, layer name and lock state. Folders indent and collapse. Footer commands create raster/vector layers/folders, duplicate, import artwork and delete. Context commands expose merge/flatten/rasterize and row colors. Multi-selection, drag reorder, inline rename and visibility sweeps are part of the existing workflow; deletion is recoverable with artwork Undo.'),
'Colors':('Pick foreground/background paint colors and manage named palettes.','The picker changes hue and saturation/value; hex entry provides exact color. Swatches select colors, palette controls choose a named collection, and available add/manage actions edit the palette. Foreground/background swapping is distinct from choosing a new palette. Color changes affect subsequent drawing, not existing pixels.'),
'Navigator':('A miniature canvas for view navigation.','The miniature supports navigation of the current sheet; zoom and view rotation pair a slider with exact entry. Fit sheet and reset rotation are direct actions. View navigation must stay responsive and separate from artwork Undo.'),
'References':('Import and inspect image references without inserting them into artwork.','Select a reference thumbnail, zoom/pan its preview and use fit/reset or available import/remove actions. Eyedropper sampling can take a color from the reference. This panel is distinct from importing an image as a canvas layer. Empty, populated, overflow and narrow examples are included.'),
'Effects':('Non-destructive effects, color correction and layer masks on the active target.','Tabs expose Mask, Blur, Shadow, Glow and Color. A master switch and per-effect switches control contribution. Blur/shadow/glow use radius and strength controls; shadow has a direct offset widget and color. Color exposes exposure/contrast/saturation/hue. Masks can be created, enabled, inverted, revealed/hidden and painted with mask-specific controls. Adjustment layers provide color correction. Preview gestures commit once; unsupported targets show disabled controls/status.'),
'Tool Properties':('A context-sensitive inspector that follows the active drawing tool.','Brush/Eraser expose only frequent drawing settings and a wrench for advanced settings. Selection and transform, guides, shapes/fill/text/retouch, vector editing and 3D use different pages. Sample source, tolerance and gap closing apply to bucket/color tools. Shapes expose fill/stroke and width; gradients expose editable stops and linear/radial mode. Retouch switches smudge/mix/blur/sharpen/dodge/burn behavior. Text exposes artwork font/layout controls. Identical inspector views shared by several tools are consolidated with context aliases.'),
'Selection and Transform':('Pixel selection controls and the ordinary raster transform inspector.','Selection tools combine with New/Add/Subtract/Intersect. The inspector provides select all, invert and deselect, expansion/contraction, feather and saved-selection controls where exposed. A pending transform uses canvas handles plus exact position/size/angle and linked proportions, with Apply/Cancel. Leaving a changed transform offers Apply/Discard/Cancel. Off-sheet preview pixels remain available until Apply crops to drawing bounds.'),
'Canvas overlays':('Interactive controls drawn over the artwork.','Examples include selection marching ants and outside hatching, a floating selection task bar, transform bounds/handles, editable vector paths, perspective assistants, motion paths and 3D rig/pose handles. These surfaces need high contrast over varied artwork, pen-sized targets, and must avoid obscuring the drawing.'),
'Guides':('Create and edit multiple perspective/ruler assistants.','Six types are implemented: perspective grid, vanishing point, two-point, parallel ruler, three-point and fisheye. Each guide has visibility, lock and snapping state. Selected settings include name, density, verticals, limits, color and opacity; shared settings control global visibility/snapping/handles/color/opacity. Canvas handles define geometry. Guide sets can be loaded/saved. Selected/shared tabs should remain clearly distinct.'),
'Advanced Brush':('The wrench dialog for detailed brush behavior.','Categories expose general settings; tip geometry and multi-tip resources; shape dynamics; scatter/count; texture; dual brush; color dynamics; transfer/opacity/flow; pen pose; extra behaviors; symmetry; and presets. Connected brushes, repeating/perspective surfaces, spray clustering and speed controls are available within the relevant categories. Response graphs, tip geometry, spacing strips, scatter, pen pose and surface widgets are interactive, not decorative previews. Captures include default and enabled dependent settings plus scrolled/narrow states.'),
'Add Tools':('Add tool definitions to a group or create a Toolbox placement.','Toolbox creation switches Group/Standalone Tool. Group mode names the group and chooses an icon and members. Tool mode searches available definitions; an independent copy can be created with its own name/settings. Adding to an existing Tools list uses that selected destination. This organization workflow is separate from painting or importing brush resources.'),
'Brush Management':('Global brush catalog maintenance, separated from drawing-time settings.','Search and tag/category/type filters narrow the catalog. Selected brushes expose name, type/category, tags, group assignment and organizational color/actions. Creation supports Pen, Ink, Soft, Spray, Stamp, Colored Stamp, Custom and Dynamic Stamp. Duplicate, delete, import and advanced settings are available. Batch selection changes metadata together. Browsing the manager must not silently change the drawing tool; use the explicit activation action.'),
'New Brush':('Define a new brush, stamp set or copy of current settings.','Choose New brush/Current brush, then a brush type. Name, category, comma-separated tags and destination group define organization. Stamp types require an image; colored stamps retain source colors. Dynamic Stamp accepts multiple images with foreground/image-color modes and per-stamp variation. Create validates the inputs; Cancel leaves the catalog unchanged. Dependent image controls appear only for appropriate types.'),
'Tool Management':('Organize Toolbox groups and tool metadata in a global manager.','Groups/Tools tabs separate group editing from batch tool editing. Group fields cover name, tags, color, built-in/custom icon and membership. The membership chooser selects which definitions belong to the group. Tools support search, multi-selection, tag/color edits and grouping. Organization Undo is distinct from artwork Undo. Browsing management does not activate a paint tool.'),
'Vector Editing':('Edit raster-style brush strokes stored as editable vector paths.','Path Edit has transform, node and local-width modes. Canvas bounds move/scale/rotate a selected path; style controls change stroke width, color and opacity. Node handles reshape geometry. Simplify Brush reduces points locally; Width Brush thickens/thins locally, with reset/simplification commands. Vector erasing operates on paths/segments. Preserve the distinction between selecting a path, selecting nodes and editing width.'),
'3D Library':('Built-in models and global imported model assets.','Tabs separate starter models and personal assets. Search/selection previews a model; placement inserts an instance into a shot 3D layer. Import stores a global copy for later reuse. Library deletion and placed scene instances are separate. 3D is primarily a drawing/posing reference workflow.'),
'3D Properties':('Inspector for a selected 3D model or the scene camera.','Object controls manage instances, visibility and transform (position/rotation/scale), fit/reset/duplicate/delete and scene display options. Camera controls orbit/pan/dolly and exact position/target/lens settings. The inspector also navigates to rig, weight and pose workflows. Values should update without panel shaking or stealing the tablet cursor.'),
'3D Rig':('Build and edit a rest skeleton inside Toon Pro.','Create a basic/humanoid skeleton, select joints in a hierarchy, add/remove joints, mirror a branch or edits, and change joint names/positions. Front/side/top views and rig visibility help placement. Rest editing defines the skeleton; weight binding and posing are separate modes.'),
'3D Weights':('Bind model geometry to a skeleton and refine deformation weights.','Unbound and bound states expose different actions. Binding associates geometry with joints. Weight painting supports Add/Subtract/Smooth, brush radius/strength and preview rotation about the selected joint. Unbind/reset controls affect binding or preview as labeled. Preserve feedback for invalid/busy states and the difference between preview and saved model pose.'),
'3D Pose':('Pose a bound rig using FK and two-bone IK.','FK rotates the selected joint with axis controls and body/root translation. IK exposes target and pole, pinning, joint limits and mode/axis choices. Mirroring/reset actions operate on the pose. Joint selection and target/pole handle selection must be visible. Included workflow fixtures show bound FK/IK states; unbound states explain unavailable controls.'),
'Timeline':('Shot-based frame animation, property animation, cameras and audio tracks.','Transport jumps to drawings/frames and plays/loops. Tracks show held exposures, property keys, camera keys and audio waveforms. Add track chooses static, frame or keyframe animation; blank/duplicate/remove act on drawings/exposures. Ruler scrubbing, playback range, onion skin, audio scrub/import and ripple editing are available. Timeline selection and frame properties must remain synchronized.'),
'Frame Properties':('The second animation panel, following the selected drawing, property layer, audio clip or camera.','Drawing shows name/start/hold. Animation exposes property channels, keys, interpolation, easing, pivot and motion-path controls, plus vector path/morph animation where compatible. Audio exposes clip timing/trim, gain/mute, fades, gain envelope and crossfade controls. Camera exposes 2D/3D framing and keys. Tab changes should preserve the current timeline selection and unfinished edits.'),
'Project Setup':('Create a movie with a first shot, or edit project/shot and production sheet settings.','Fields cover movie name, shot number/name, dimensions/aspect, frame rate/duration and sheet margins/header/overscan/safe area/transparency, with a live preview. Production sheets include the header and margins; movie/frame exports use the artwork frame. Configuration and error feedback should make those two coordinate spaces clear.'),
'Onion Skin':('Configure preceding/following drawing previews.','Previous/Next counts, opacity and separate tint swatches control the overlay. Reset restores defaults; OK keeps settings and Cancel restores the prior preview configuration. These are workspace display settings, not artwork layers.'),
'Timeline Editing':('Insert, delete or move a time range across shot tracks.','Mode tabs change the required start/count/destination inputs. Scope and result summaries explain what changes across exposures, keys, audio and duration before Apply. Cancel leaves the timeline unchanged.'),
'Timeline dialogs':('Add-track and playback-range configuration.','Add Track selects Static/Frame by frame/Keyframe; drawing-template rows define layered recipes and can be edited/saved. Playback range specifies first/last frames and uses the shot frame count. Both dialogs must communicate frame numbering and pending-edit handling.'),
'Add Track':('Create a static, frame-by-frame or property-animation track.','Track type determines whether drawing-template controls or keyframe setup is relevant. Frame templates define rough/sketch/line/color/shadow/highlight layer recipes, support row editing and preset management, and are independent of actual artwork content.'),
'Video Export':('Export the current shot, a whole movie or queued shots.','Scope, range, output dimensions/aspect, quality, audio inclusion and destination are reviewed before Export. Queue scope includes per-shot review and overwrite decisions. Progress/cancel/error feedback belongs to long-running export. These captures show configuration, not a claim that an export was run.'),
'Audio Export':('Render the shot or movie audio mix to WAV.','Scope and full/custom frame range choose the mix duration, and destination chooses the file. Summary/status, Export and Cancel explain validity and progress. Clip effects/fades/envelopes are included through the current mixer behavior.'),
'Shots':('Manage movie shots/scenes without replacing the drawing workspace.','The list exposes shot number/name/length and current selection. Scene filtering/assignment, create/duplicate/delete/reorder and selected-shot export support organization. Selecting a shot resolves pending edits before changing the active document context.'),
'History':('Navigate retained artwork Undo/Redo states.','Current, saved and later redo states are visually distinct. Selecting a history row returns to that command; Undo/Redo buttons use the same artwork stack. New edits after Undo replace the redo branch. The oldest retained baseline is labeled when limits remove earlier history. Live pen strokes cannot be interrupted; pending transforms keep Apply/Discard/Cancel.'),
'Auto Actions':('Record and replay reusable semantic artwork workflows.','Create/name an action, Record, perform supported operations, Stop and Play on a target. Supports layer setup/properties/effects, selection fills, paint buckets and applied single-raster-layer move/resize/rotate. Step checkboxes skip steps; menus reorder/remove or rename/duplicate/delete actions. Replay validates first and commits in one Undo. Manual selection creation, freehand strokes, warp and multi-layer transforms remain outside recording; unsupported operations stop visibly.'),
'Decisions':('Resolve pending artwork changes before a context switch.','Apply commits the current transform, Discard restores the prior artwork, and Cancel keeps the current tool/preview active. Do not remove this three-way choice while reducing routine clicks.'),
'Application menus':('The complete menu command map, including less frequent operations.','File, Edit, Select, View and Panels supplement icon surfaces and shortcuts. Disabled menu items reflect current document/selection/gesture context. Menu text and shortcuts in the screenshots are the implemented platform wording.'),
}
ALIAS={
 'vector_edit_panel':'Vector Editing','references':'References','tool_navigation_panel':'Tools','camera_panel':'Frame Properties','effects_panel':'Effects','timeline_edit_panel':'Timeline','audio_panel':'Frame Properties','drawing_tool_panel':'Tool Properties','rig_workspace':'Canvas overlays','model_library_panel':'3D Library','rig3d_panel':'3D Rig','motion_canvas':'Canvas overlays','scene_workspace':'3D Properties','drawing_mode':'Workspace','motion_panel':'Frame Properties','canvas_feedback':'Workspace','skin3d_panel':'3D Weights','video_export_sequence':'Video Export','skin_workspace':'Canvas overlays','advanced_motion_panel':'Frame Properties','pose_workspace':'Canvas overlays','shots_panel':'Shots','keyframe_panel':'Frame Properties','tool_management_panel':'Tool Management','audio_export_dialog':'Audio Export','pose3d_panel':'3D Pose','timeline_panel':'Timeline','brush_management_panel':'Brush Management','timeline_improvements':'Timeline dialogs','audio_finishing_panel':'Frame Properties','video_export_dialog':'Video Export','animation_workspace':'Workspace','text_workflow':'Tool Properties','palette_panel':'Colors','brush_preset_panel':'Brush Preset','advanced_brush_tips':'Advanced Brush','brush_surface_pattern':'Advanced Brush','advanced_dynamics':'Advanced Brush','dynamic_stamp_storage':'Advanced Brush'
}
def clean(s):return re.sub(r'\s+',' ',str(s or '')).strip()
def anchor(s):return re.sub('[^a-z0-9]+','-',s.lower()).strip('-')
def main():
 if not (OUT/'capture-manifest.json').exists() and (OUT/'screen-manifest.json').exists():
  screens=json.loads((OUT/'screen-manifest.json').read_text())['screens'];families=defaultdict(list)
  for screen in screens:families[screen['family']].append(screen)
  fixtures=json.loads((OUT/'evidence/fixture-manifest.json').read_text())
  make_index(screens,families);make_guide(screens,families,fixtures);copy_references();write_brief(screens,families)
  print('Rebuilt handoff documents for',len(screens),'verified screenshots.');return
 primary=json.loads((OUT/'capture-manifest.json').read_text())
 # Retain the unmodified capture inventory in evidence; consolidate identical PNGs below.
 raw=OUT/'evidence'/'raw-capture-manifest.json'
 if not raw.exists():raw.write_text(json.dumps(primary,ensure_ascii=False,indent=2))
 screens=primary['screens']
 extra=OUT/'extra'/'capture-manifest.json'
 if extra.exists():
  for s in json.loads(extra.read_text())['screens']:
   s={**s,'file':'extra/'+s['file'],'source_capture_id':s['id']};screens.append(s)
 # Three first-pass MainWindow states left the welcome page up while exercising docks.
 # Replace those misleading overview views with corrected openProject screenshots.
 excluded={'Drawing workspace — populated project','Final drawing layout','Tab drawing mode — quick access only'}
 screens=[s for s in screens if not(s['family']=='Workspace' and s['state'] in excluded)]
 fixtures=json.loads((OUT/'fixture-manifest.json').read_text()) if (OUT/'fixture-manifest.json').exists() else []
 for r in fixtures:
  for f in r['screenshots']:
   im=Image.open(OUT/f)
   screens.append({'family':ALIAS.get(Path(r['source']).stem.removeprefix('test_').removesuffix('_tests'),'Additional workflow examples'),'state':Path(f).stem.replace('-',' '),'file':f,'class':'Real application visual fixture','logical_width':im.width/2,'logical_height':im.height/2,'pixel_width':im.width,'pixel_height':im.height,'scale':2,'controls':[],'fixture':r['source'],'fixture_result':r['returncode']})
 # A screenshot may represent several conditions; keep each alias but one physical image.
 unique=[];hashes={};duplicates=[]
 for s in screens:
  f=OUT/s['file'];digest=hashlib.sha256(f.read_bytes()).hexdigest()
  if digest in hashes:
   keep=hashes[digest];keep['also_shown_for'].append({'family':s['family'],'state':s['state']});duplicates.append(s['file']);continue
  s={**s,'sha256':digest,'also_shown_for':[]};hashes[digest]=s;unique.append(s)
 for i,s in enumerate(unique,1):s['screen_id']=f'S{i:03d}'
 (OUT/'screen-manifest.json').write_text(json.dumps({'application':'Toon Pro','version':'0.50.1','date':'2026-10-31','screens':unique,'coverage_note':'Meaningful implemented UI states and fixture examples, not every possible numeric value, artwork, interaction timing or OS dialog. Identical images have context aliases.'},ensure_ascii=False,indent=2))
 with (OUT/'SCREEN-LIST.csv').open('w',newline='') as f:
  w=csv.writer(f);w.writerow(['Screen ID','Panel / surface','State','Screenshot','Width (logical)','Height (logical)','Pixel width','Pixel height','Shared-state aliases','Control reference available'])
  for s in unique:w.writerow([s['screen_id'],s['family'],s['state'],s['file'],s['logical_width'],s['logical_height'],s['pixel_width'],s['pixel_height'],len(s['also_shown_for']),bool(s['controls'])])
 # Preview thumbnails keep the index fast even with hundreds of full-resolution images.
 (OUT/'thumbnails').mkdir(exist_ok=True)
 for s in unique:
  im=Image.open(OUT/s['file']).convert('RGB');im.thumbnail((440,360));im.save(OUT/'thumbnails'/(s['screen_id']+'.jpg'),quality=85)
 families=defaultdict(list)
 for s in unique:families[s['family']].append(s)
 make_index(unique,families)
 make_guide(unique,families,fixtures)
 copy_references()
 write_brief(unique,families)
 # Keep source evidence separate from the primary entry points.
 shutil.copy2(ROOT/'build/ux-capture-build.log',OUT/'evidence'/'capture-build.log')
 shutil.copy2(ROOT/'build/ux-capture-run.log',OUT/'evidence'/'capture-run.log')
 (OUT/'evidence'/'capture-coverage.json').write_text(json.dumps({'raw_widget_capture_count':len(primary['screens']),'unique_screenshot_count':len(unique),'families':dict(Counter(s['family'] for s in unique)),'fixture_runs':len(fixtures),'fixture_failures':[{'source':r['source'],'result':r['returncode'],'log':r['log']} for r in fixtures if r['returncode']!=0]},indent=2))
 # Remove only exact duplicate PNGs produced for this handoff; never touch source assets.
 used={s['file'] for s in unique}
 for f in set(duplicates):
  if f not in used:(OUT/f).unlink(missing_ok=True)
 print(json.dumps({'screenshots':len(unique),'families':len(families),'fixture_runs':len(fixtures),'folder':str(OUT)},indent=2))
CSS='''*{box-sizing:border-box}body{margin:0;font:15px/1.5 system-ui,-apple-system,sans-serif;background:#151719;color:#e5e7e9}a{color:#a7d8ec}header{padding:26px 32px;background:#202326;border-bottom:1px solid #3d4248}h1{margin:0 0 8px;font-size:28px}h2{margin-top:36px;font-size:23px}h3{font-size:17px}p{max-width:1000px}main{padding:20px 32px}.muted,small{color:#adb5be}input,select,button{font:inherit;border:1px solid #505860;border-radius:6px;background:#272c30;color:#eee;padding:8px}button{cursor:pointer}nav{display:flex;gap:18px;flex-wrap:wrap}.bar{display:flex;gap:10px;padding:16px 32px;position:sticky;top:0;background:#1a1e21;z-index:2;flex-wrap:wrap}.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(270px,1fr));gap:18px}.card{background:#22272b;border:1px solid #3b434b;border-radius:8px;overflow:hidden}.picture{height:250px;background:#101214;display:flex;align-items:center;justify-content:center;cursor:zoom-in}.picture img{max-width:100%;max-height:100%;object-fit:contain}.caption{padding:12px}.caption p{margin:4px 0}section{scroll-margin-top:90px}table{border-collapse:collapse;width:100%;font-size:13px;margin:15px 0}td,th{border:1px solid #414a52;padding:8px;text-align:left;vertical-align:top;overflow-wrap:anywhere}th{background:#272e34}code{background:#293038;padding:2px 4px}details{margin:12px 0;padding:10px;border:1px solid #414a52;border-radius:5px}summary{cursor:pointer}.thumbs{display:flex;gap:10px;overflow:auto;padding:10px 0}.thumbs img{height:150px;max-width:280px;object-fit:contain;background:#111}.badge{display:inline-block;font-size:12px;background:#363e45;border-radius:4px;padding:2px 6px;margin-right:5px}#viewer{width:96vw;max-width:96vw;height:94vh;background:#111;color:#eee;border:1px solid #666;padding:14px}#viewer::backdrop{background:#000c}#viewer img{max-width:100%;height:calc(100% - 65px);object-fit:contain;display:block;margin:auto}#viewer .line{display:flex;align-items:center;gap:14px}#viewer p{margin:7px 0;font-size:13px}.notice{border-left:3px solid #9fb4c2;padding:4px 16px;background:#20272c}pre{white-space:pre-wrap;overflow-wrap:anywhere}'''
def make_index(screens,families):
 data=[{k:v for k,v in s.items() if k in ['screen_id','family','state','file','logical_width','logical_height','pixel_width','pixel_height','also_shown_for']} for s in screens]
 options=''.join(f'<option>{E(f)}</option>' for f in sorted(families))
 content=f'''<!doctype html><html><meta charset="utf-8"><title>Toon Pro · UX screenshot atlas</title><style>{CSS}</style><header><h1>Toon Pro · UX redesign handoff</h1><p>Current implemented interface · 0.50.1 · 31 October 2026 · {len(screens)} unique screenshots across {len(families)} surface families</p><nav><a href="PANEL-GUIDE.html">Panel & control guide</a><a href="AI-DESIGN-BRIEF.txt">AI designer brief</a><a href="SCREEN-LIST.csv">Screen list</a><a href="COVERAGE.html">Coverage & limitations</a></nav></header><div class="bar"><input id="search" aria-label="Search screenshots" placeholder="Search panel, state, tool…" style="min-width:300px"><select id="family" aria-label="Filter panel"><option value="">All panels</option>{options}</select><span id="count"></span></div><main><p class="notice">These are captures of the running application widgets with synthetic sample documents, not proposed designs. Click a thumbnail to enlarge; use Open original for full resolution. Duplicate views are consolidated and retain their context aliases.</p><div class="grid" id="grid"></div><p id="empty" hidden>No matching screens.</p></main><dialog id="viewer"><div class="line"><button id="close">Close</button><a id="original" target="_blank">Open original</a><a id="reference">Panel guide</a><span id="heading"></span></div><p id="detail"></p><img id="large" alt=""></dialog><script>const screens={json.dumps(data,ensure_ascii=False).replace('</','< /')};
const el=id=>document.getElementById(id);function draw(){{const q=el('search').value.toLowerCase(),f=el('family').value;const v=screens.filter(s=>(!f||s.family===f)&&(s.family+' '+s.state+' '+JSON.stringify(s.also_shown_for)).toLowerCase().includes(q));el('grid').replaceChildren();for(const s of v){{let c=document.createElement('article');c.className='card';let p=document.createElement('div');p.className='picture';let im=new Image();im.src='thumbnails/'+s.screen_id+'.jpg';im.alt=s.family+' — '+s.state;im.loading='lazy';p.append(im);p.onclick=()=>open(s);let t=document.createElement('div');t.className='caption';let b=document.createElement('strong');b.textContent=s.screen_id+' · '+s.family;let desc=document.createElement('p');desc.textContent=s.state;let small=document.createElement('small');small.textContent=s.pixel_width+' × '+s.pixel_height+' px'+(s.also_shown_for.length?' · '+s.also_shown_for.length+' shared states':'');t.append(b,desc,small);c.append(p,t);el('grid').append(c)}}el('count').textContent=v.length+' screens';el('empty').hidden=v.length>0}}
function open(s){{el('large').src=s.file;el('large').alt=s.state;el('heading').textContent=s.screen_id+' · '+s.family;el('detail').textContent=s.state;el('original').href=s.file;el('reference').href='PANEL-GUIDE.html#'+s.family.toLowerCase().replace(/[^a-z0-9]+/g,'-').replace(/^-|-$/g,'');el('viewer').showModal()}}el('close').onclick=()=>el('viewer').close();el('search').oninput=draw;el('family').onchange=draw;draw();</script></html>'''
 (OUT/'START-HERE.html').write_text(content)
def option_rows(screens):
 rows={}
 for s in screens:
  for c in s.get('controls',[]):
   typ=c.get('type','');name=clean(c.get('name') or c.get('text') or c.get('placeholder') or c.get('id'))
   help=clean(c.get('help') or c.get('description'))
   if not name and not help:continue
   if typ in ['QLabel','toon::TimelineContext','toon::ElidedLabel']:continue
   if typ in ['QLineEdit','QDoubleSpinBox','QSpinBox','QSlider'] and (c.get('id','').startswith('qt_') or name in ['qt_spinbox_lineedit','']):continue
   if typ.startswith('toon::') and not help and not c.get('name') and not any(x in typ for x in ['Widget','ValueSlider']):continue
   # Deduplicate changing selected values; the manifest retains each exact state.
   key=(name,typ,help)
   if key not in rows:rows[key]={'name':name,'type':typ,'help':help,'choices':c.get('choices',[]),'tabs':c.get('tabs',[]),'actions':c.get('actions',[]),'minimum':c.get('minimum'),'maximum':c.get('maximum'),'examples':[]}
   if s['screen_id'] not in rows[key]['examples'] and len(rows[key]['examples'])<3:rows[key]['examples'].append(s['screen_id'])
 return rows.values()
def make_guide(screens,families,fixtures):
 parts=[f'<!doctype html><html><meta charset="utf-8"><title>Toon Pro · Panel and control reference</title><style>{CSS}</style><header><h1>Panel & control reference</h1><nav><a href="START-HERE.html">Screenshot atlas</a><a href="AI-DESIGN-BRIEF.txt">Design brief</a><a href="COVERAGE.html">Coverage</a></nav></header><main><p>This file explains the implemented panels and options for the UX designer. Control names/help are extracted from real widgets; choices and ranges are observed application values, not redesign suggestions. Screen IDs map to SCREEN-LIST.csv and the atlas. Supplementary workflow images have no per-widget extraction; use their panel description and the matching detailed captures.</p><h2>Workflow map</h2><p>Create/open movie → choose shot → select raster/vector/3D target → choose Toolbox group and Tool → adjust Tool Properties → work on canvas → manage layers/colors/references → use History/Auto Actions → save/export. Detailed brush editing and global organization are occasional workflows. Timeline/Frame Properties form a paired animation workflow, included for completeness.</p><h2>Panel navigation</h2><nav>'+''.join(f'<a href="#{anchor(f)}">{E(f)}</a>' for f in sorted(families))+'</nav>']
 allrows=[]
 for f in sorted(families):
  desc,behavior=ABOUT.get(f,('Additional real workflow views.','Use the captured labels, tooltips and the linked screen list for the implemented behavior.'))
  parts.append(f'<section id="{anchor(f)}"><h2>{E(f)}</h2><p><strong>{E(desc)}</strong></p><p>{E(behavior)}</p><p class="muted">{len(families[f])} unique views. Open the atlas and filter by this panel to inspect all states.</p><div class="thumbs">')
  for s in families[f][:8]:parts.append(f'<a href="{E(s["file"])}" title="{E(s["screen_id"]+" · "+s["state"])}"><img loading="lazy" src="thumbnails/{s["screen_id"]}.jpg" alt="{E(s["state"])}"></a>')
  parts.append('</div>')
  rows=list(option_rows(families[f]));parts.append(f'<details><summary>Control reference · {len(rows)} observed controls</summary><table><tr><th>Control / function</th><th>What it does / help</th><th>Choices or bounds</th><th>Examples</th></tr>')
  for row in rows:
   choices=row['choices'] or [t.get('text') or t.get('help') for t in row['tabs']] or [t['text'] for t in row['actions']]
   bounds=' / '.join(choices)
   if row['minimum'] is not None:bounds=f'{row["minimum"]} … {row["maximum"]}'
   help=row['help'] or 'Named control; its current state is visible in the example. See the panel workflow description above.'
   parts.append(f'<tr><td>{E(row["name"])}<br><small>{E(row["type"])}</small></td><td>{E(help)}</td><td>{E(bounds)}</td><td>{E(", ".join(row["examples"]))}</td></tr>')
   allrows.append({'panel':f,**row})
  parts.append('</table></details><details><summary>All captured states</summary><ul>')
  for s in families[f]:parts.append(f'<li><a href="{E(s["file"])}">{s["screen_id"]} · {E(s["state"])}</a>'+(' <small>Also represents '+E('; '.join(x['state'] for x in s['also_shown_for'][:5]))+'</small>' if s['also_shown_for'] else '')+'</li>')
  parts.append('</ul></details></section>')
 parts.append('</main></html>');(OUT/'PANEL-GUIDE.html').write_text(''.join(parts));(OUT/'control-reference.json').write_text(json.dumps(allrows,ensure_ascii=False,indent=2))
 coverage=f'''<!doctype html><html><meta charset="utf-8"><title>Toon Pro · Coverage</title><style>{CSS}</style><header><h1>Coverage and capture notes</h1><a href="START-HERE.html">Back to atlas</a></header><main><p>Version 0.50.1, captured 31 October 2026 on macOS using the application's Qt widgets and theme. Widget views are rendered at 2× scale with temporary synthetic artwork, libraries and settings. These are current UI captures rather than design mockups. Real-window visual fixtures supplement the inventory with bound 3D rigs, audio and animation examples.</p><p><strong>{len(screens)} unique screenshots · {len(families)} surface families · {len(fixtures)} supplementary fixture runs.</strong> Identical PNGs reached through different paths are represented once with state aliases. The raw capture inventory is in evidence; screen-manifest.json is the authoritative delivered list.</p><h2>Included conditions</h2><p>No project/empty states; project open and populated; available/disabled controls; selection/transform pending and decision dialog; supported tool inspectors; advanced brush category tabs and dependent settings; multiple guides; vector path edits; 3D object/camera/rest/weights/FK/IK; frame/keyframe/audio/camera animation; recorded action states; narrow/short/scrolled layouts; custom creation/configuration dialogs; principal context/application menus; drawing mode.</p><h2>Limits to interpret honestly</h2><p>“Each condition” means meaningful implemented UX states, not every combination of numeric value, tool preset, color, artwork, screen size, focus/hover timing or operating-system state. System file pickers, arbitrary OS permissions, crash dialogs and every possible runtime export/import error are not exhaustively captured. Export configuration views do not imply export jobs were run. The sample documents are illustrative, not the artist's production artwork. Platform window title bars, shadows and native file dialogs can differ from these widget-only PNG captures. Physical Huion feel cannot be inferred from still screenshots.</p><p>Fixtures that could not run are identified in evidence/capture-coverage.json and their logs; their output, if any, is labeled in screen-manifest.json. Do not infer new features from an illustrative sample or from a historical reference document. Current screenshots and implemented control help take precedence over older development notes.</p><h2>Traceability</h2><p>SCREEN-LIST.csv lists the screen IDs, states and paths. screen-manifest.json contains dimensions, aliases, source fixture and exact widget/control states. control-reference.json is the consolidated option dictionary. reference/ contains the original project behavior documents and UI rules. evidence/ contains capture logs and coverage counts. All navigation works locally; no server or external asset service is required.</p></main></html>'''
 (OUT/'COVERAGE.html').write_text(coverage)
def copy_references():
 names=['ui-guidelines','drawing-tools','brush-settings','brush-compatibility','layers','selections','guides','history-and-actions','animation','requirements','architecture']
 for name in names:
  p=ROOT/'docs'/(name+'.md')
  if p.exists():(OUT/'reference'/(name+'.txt')).write_text(p.read_text())
 (OUT/'reference'/'theme-tokens.h.txt').write_text((ROOT/'src/ui_theme.h').read_text())
 (OUT/'reference'/'README.txt').write_text('These are copies of existing implementation/reference documents. Some include historical release notes and planned requirements. Read current captures and PANEL-GUIDE.html first. Do not treat old plans as shipped features. Theme tokens are the current shared application values.\n')
def write_brief(screens,families):
 text=f'''TOON PRO — AI UX DESIGNER HANDOFF
Application: Toon Pro 0.50.1
Captured: 31 October 2026
Deliverable: {len(screens)} unique real screenshots across {len(families)} UI surface families.

START HERE
1. Open START-HERE.html. Search/filter by panel and click any screenshot to enlarge.
2. Read PANEL-GUIDE.html for panel purposes, workflows and the observed control dictionary.
3. Use SCREEN-LIST.csv to enumerate views, and screen-manifest.json/control-reference.json when machine-readable detail is useful.
4. Read COVERAGE.html before assuming every hypothetical state is represented.
5. reference/ contains copied behavior documents, artist UI rules and shared theme tokens. Historical plans are not current feature claims.

PRODUCT AND ARTIST
A desktop drawing/animation production app for a solo hand-drawn anime filmmaker using a Huion display tablet. The app combines raster painting, editable vector brush paths, 3D drawing references/rig posing and animation. The current redesign priority is the everyday drawing workspace. Animation and 3D screens are included so the design language remains coherent across the application.

DESIGN ASSIGNMENT
Redesign the current interface for comfortable, efficient pen-driven work. Preserve the existing functionality and artist's organization. Propose a coherent compact component system, updated layout hierarchy and clear visual states; do not remove features to make screenshots look cleaner. Focus first on Toolbox, Tools, quick Tool Properties, Layers, Colors, Navigator and References, then detailed brush editing, management, effects and 3D/animation inspectors.

ARTIST'S INTERACTION RULES
- Minimize clicks for frequent actions; keep pen work and canvas focus predictable.
- Prefer recognizable icons with accessible names and the shared 600 ms tooltip delay. Keep visible text for names, values, ambiguous choices and decisions.
- Compact visual size must retain comfortable pen targets and consistent icon sizing/strokes.
- Use neutral charcoal surfaces, silver/gray emphasis and restrained semantic color. The artist rejected saturated blue selection chrome. Follow shared semantic theme roles, inspired by Adobe Spectrum rather than copying another app wholesale.
- Simple scalar settings use a label and synchronized slider/exact-entry pair on one row when space permits; long labels wrap above the pair. Number boxes are compact (currently 48 logical px), with units outside the input; three-digit visual sizing is not a numeric value limit.
- Use meaningful visual widgets for coupled/complex settings: response graphs, tip shape, spacing, scatter, color, pen pose, shadow offset, surface perspective and canvas handles.
- Use persistent tabs for feature/category switching. Avoid hiding frequent actions behind successive menus.
- Normal docks resize/float/tab. Fixed-width icon columns span the side; Toolbox uses this pattern.
- Brush quick properties stay small; the wrench opens advanced configuration. Brush/tool management is an occasional global-library workflow.
- Tag/search filters must not accidentally change the active brush. Global organization, artwork history and view preferences are different scopes.
- Canvas cursor follows brush shape/size and restores correctly after crossing panel splitters. Disabled actions need clear cursor/status feedback. Avoid panel shaking, focus loss or extra redraw work during strokes.
- Tab drawing mode preserves quick access and returns to the previous workspace layout.
- Changed transforms retain Apply/Discard/Cancel; deleting layers is recoverable with Undo. These are workflow safeguards, not arbitrary extra clicks.

WHAT TO DELIVER
- A complete screen/state map cross-referenced to the supplied screen IDs.
- Redesigned everyday drawing workspace at wide and compact desktop widths.
- Reusable component specifications: panel headers, icon buttons, tabs, property rows, numeric pairs, lists/trees, filter rows, previews, menus, dialogs, tooltips and feedback.
- Empty, populated, selected, disabled, error, recording/playback and pending-edit states.
- Detailed brush, tool/brush manager and advanced inspector layouts that preserve access to all existing options.
- Theme tokens, spacing/icon/typography scales, focus/contrast and pen-target specifications.
- An explicit migration map from each existing control/workflow to the proposed location. Flag proposed behavior changes for discussion rather than silently changing semantics.

HOW TO READ THIS PACK
PNG files are actual application widget captures with synthetic sample content, rendered at 2×. They are not AI-generated designs. Identical visual states are consolidated with context aliases. Some long panels have top/middle/bottom views. Supplementary fixture images show richer populated workflows and do not include per-widget extraction; matching panel descriptions and primary inspector captures provide the option reference.

IMPORTANT LIMITS
This is a representative state atlas of the implemented interface, not an exhaustive enumeration of all values, every library item, every runtime failure or native OS dialog. Current screenshots and control help take precedence over historical notes in reference/. Still images cannot certify Huion drawing feel or performance. No proposed redesign has been implemented by this handoff task.
'''
 (OUT/'AI-DESIGN-BRIEF.txt').write_text(text)
 (OUT/'README.txt').write_text('Open START-HERE.html in a browser.\nGive this entire folder to the UX designer, starting with AI-DESIGN-BRIEF.txt and PANEL-GUIDE.html.\nAll screenshots are local PNGs; thumbnails are browsing aids. SCREEN-LIST.csv enumerates the delivery.\nThe current version is Toon Pro 0.50.1. Capture date: 31 October 2026.\n')
if __name__=='__main__':main()
