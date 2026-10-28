"""Generate a method-level migration index from this fork's actual Lua wrappers.

A mapping is a porting destination, not a claim of matching overloads or semantics.
Unmapped methods remain visible. Native destinations must exist in declarations; SCUA facade exports are resolved from their source.
"""
import argparse
from collections import Counter
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parent.parent
NATIVE = {f'{p.stem}.{name}' for p in (ROOT/'scua/decls').glob('*.scua')
          for name in re.findall(r'^fn (\w+)\(', p.read_text(), re.M)}
NATIVE_PARAMS = {f'{p.stem}.{name}':args for p in (ROOT/'scua/decls').glob('*.scua')
                 for name,args in re.findall(r'^fn (\w+)\(([^)]*)\)',p.read_text(),re.M)}
OBJECT_PARAMS = {
    'Texture':{'texture','image'}, 'Shader':{'shader'}, 'ParticleSystem':{'particles'},
    'Body':{'body'}, 'World':{'world'}, 'Joint':{'joint'}, 'ImageData':{'data','image'},
    'Source':{'source'}, 'SoundData':{'data'}, 'VideoStream':{'video'},
    'RandomGenerator':{'generator'}, 'BezierCurve':{'curve'}, 'Transform':{'transform'},
}
MODULE_GROUPS = {
    'audio.Audio', 'data.DataModule', 'event.Event', 'font.Font', 'graphics.Graphics',
    'image.Image', 'joystick.JoystickModule', 'keyboard.Keyboard', 'math.Math',
    'mouse.Mouse', 'physics.Physics', 'sensor.Sensor', 'sound.Sound', 'system.System',
    'timer.Timer', 'touch.Touch', 'video.Video', 'window.Window',
}
OVERRIDES = {}
def map_methods(group, target, methods, note='', status='Adapted'):
    for method in methods.split():
        OVERRIDES[group, method] = (status, target, note)



# Explicit exceptions and host/language replacements.
for method in 'getPointer getFFIPointer performAtomic'.split():
    map_methods('data.Data', '', method, 'Raw native pointers and Lua atomic callbacks are not exposed.', 'Excluded')
map_methods('data.DataModule', '', 'newDataView newByteData pack unpack getPackedSize', 'Use SCUA bytes/packed arrays. Lua pack format strings are not implemented.', 'Replacement')
map_methods('filesystem.Filesystem', '', 'init setFused isFused _setAndroidSaveExternal setSource getSource getCRequirePath setCRequirePath', 'Lua bootstrap, fused packaging or platform/distribution behavior; outside this batch.', 'Excluded')
map_methods('filesystem.Filesystem', '', 'openNativeFile mountFullPath mountCommonPath unmountFullPath unmountCommonPath getFullCommonPath getWorkingDirectory getUserDirectory getAppdataDirectory getRealDirectory canonicalizeRealPath setSymlinksEnabled areSymlinksEnabled', 'Ambient host-path access is intentionally replaced by grants and selected-file dialog capabilities.', 'Excluded')
for cls, methods in {
    'Channel':'push supply pop demand peek getCount hasRead clear performAtomic',
    'LuaThread':'start wait getError isRunning',
    'ThreadModule':'newThread newChannel getChannel',
}.items():
    map_methods('thread.'+cls,'',methods,
                'SCUA actors/tell/ask/spawn/wait replace Lua threads/channels. Host budgets/workers are implemented; shared mutable channels/atomic callbacks are not reproduced.', 'Replacement')
map_methods('event.Event', 'love_event.post', 'push', 'Queued main-thread custom events; not a thread-safe actor inbox.')
map_methods('event.Event', 'love_event.reload', 'restart', 'Reloads SCUA definitions/resources with optional logical snapshot; not process restart.')
map_methods('event.Event', '', 'pump poll_i wait clear setModalDrawCallback _setDefaultModalDrawCallback', 'Host owns SDL pumping, frame delivery and draw callbacks.', 'Host-owned')
map_methods('timer.Timer', '', 'step', 'Host advances frame timing and passes dt to update.', 'Host-owned')
map_methods('graphics.Graphics', '', 'present', 'Host presents after draw and delivers screenshot results afterward.', 'Host-owned')
map_methods('graphics.Graphics', '', 'getQuadIndexBuffer', 'Internal shared GPU index buffer is not exposed.', 'Excluded')
map_methods('window.Window', '', 'getPointer', 'Native OS pointers are not exposed.', 'Excluded')
map_methods('system.System', 'love_system.has_background_music', 'hasBackgroundMusic', 'Matches upstream desktop false; mobile background-music probes remain platform work.')
for cls in 'Body Shape Joint'.split():
    map_methods('physics.'+cls, '', 'setUserData getUserData', 'Store typed game records in SCUA keyed by stable world/body/collider/joint IDs.', 'Replacement')
map_methods('physics.Physics', '', 'getMeter setMeter', 'Meter scale is selected per world in new_world; no process-global mutable meter.', 'Replacement')

# Module functions and renamed methods. Every destination is validated below.
def aliases(group, module, pairs):
    for method, target in pairs.items():
        map_methods(group, 'love_'+module+'.'+target, method)

aliases('window.Window','window', {'getMode':'mode','setMode':'configure','updateMode':'configure','getTitle':'title','isHighDPIAllowed':'high_dpi_allowed','getIcon':'icon'})
aliases('graphics.Graphics','graphics', {
 'newCubeTexture':'new_texture','newArrayTexture':'new_texture','newVolumeTexture':'new_texture',
 'newArrayImage':'new_texture','newVolumeImage':'new_texture','newCubeImage':'new_texture','newTextureView':'texture_view',
 'newParticleSystem':'new_particles','newTextBatch':'new_text','setNewFont':'new_font',
 'getWidth':'dimensions','getHeight':'dimensions','getDimensions':'dimensions','getPixelWidth':'pixel_dimensions','getPixelHeight':'pixel_dimensions',
 'getSupported':'is_supported','getSystemLimits':'system_limit','getTextureFormats':'texture_format_supported','getCanvasFormats':'texture_format_supported','getImageFormats':'texture_format_supported',
 'push':'push_state','pop':'pop_state','getStackDepth':'stack_depth','setCanvas':'set_render_targets',
 'readbackBuffer':'buffer_read','readbackBufferAsync':'buffer_read_async','readbackTexture':'texture_read','readbackTextureAsync':'texture_read_async',
 'getScissor':'scissor','getBlendMode':'blend_mode','getDepthMode':'depth_mode','getStencilMode':'stencil_mode',
 'dispatchThreadgroups':'dispatch_threadgroups','newImageFont':'new_font_from_rasterizer'})
aliases('graphics.Texture','graphics', {'replacePixels':'texture_replace','generateMipmaps':'texture_generate_mipmaps','newImageData':'texture_read','setFilter':'texture_set_sampler','setWrap':'texture_set_sampler','setMipmapFilter':'texture_set_sampler','setDepthSampleMode':'texture_set_sampler'})
map_methods('graphics.Texture', '', 'renderTo', 'Use set_canvas/set_render_targets in draw(), then restore the target explicitly.', 'Replacement')
aliases('graphics.Buffer','graphics',{'setArrayData':'buffer_set_data','clear':'buffer_clear'})
aliases('graphics.Mesh','graphics', {'setVertex':'mesh_set_vertex','getVertex':'mesh_vertex_bytes','setVertexMap':'mesh_set_vertex_map','setTexture':'mesh_set_texture','getTexture':'mesh_texture','setDrawMode':'mesh_configure','setDrawRange':'mesh_configure','setAttributeEnabled':'mesh_attribute_enabled','attachAttribute':'mesh_attach_attribute','detachAttribute':'mesh_detach_attribute','getVertexBuffer':'mesh_vertex_buffer','setIndexBuffer':'mesh_set_index_buffer','getIndexBuffer':'mesh_index_buffer','flush':'mesh_flush'})
map_methods('graphics.Mesh','love_graphics.buffer_set_data love_graphics.mesh_vertex_buffer','setVertices setVertexAttribute','Edit/upload the vertex buffer; no Lua variadic per-attribute overloads.')
map_methods('graphics.Mesh','love_graphics.mesh_vertex_bytes love_graphics.mesh_info','getVertexAttribute','Read bytes using format/offset metadata; no automatic unpacking.')
aliases('graphics.SpriteBatch','graphics',{'add':'batch_add','set':'batch_set','addLayer':'batch_layer','setLayer':'batch_layer','clear':'batch_clear','flush':'batch_flush','setTexture':'batch_set_texture','getTexture':'batch_texture','setColor':'batch_set_color','setDrawRange':'batch_set_range'})
aliases('graphics.Quad','graphics',{'setViewport':'quad_set_viewport'})
aliases('graphics.TextBatch','graphics',{'set':'text_set','setf':'text_set_wrapped','add':'text_add','addf':'text_add_wrapped','clear':'text_clear','setFont':'text_set_font','getFont':'text_font','getWidth':'text_dimensions','getHeight':'text_dimensions','getDimensions':'text_dimensions'})
aliases('graphics.Shader','graphics',{'getWarnings':'shader_warnings','send':'shader_send','hasUniform':'shader_has_uniform','hasStage':'shader_has_stage','getLocalThreadgroupSize':'shader_threadgroup_size','getBufferFormat':'shader_buffer_format'})
aliases('graphics.GraphicsReadback','graphics',{'isComplete':'readback_status','hasError':'readback_status','wait':'readback_wait','update':'readback_status','getBufferData':'readback_bytes','getImageData':'readback_image'})
aliases('graphics.Font','graphics',{'getWidth':'font_width','getWrap':'font_wrap_info','setLineHeight':'font_set_line_height','setFilter':'font_set_filter','getFilter':'font_filter','hasGlyphs':'font_has_glyphs','getKerning':'font_kerning','setFallbacks':'font_set_fallbacks'})
aliases('graphics.ParticleSystem','graphics',{'clone':'particles_clone','setTexture':'particles_set_texture','getTexture':'particles_texture','setQuads':'particles_set_quads','getQuads':'particles_quads'})
aliases('font.Font','font',{'newTrueTypeRasterizer':'new_rasterizer','newGlyphData':'new_glyph'})
aliases('font.Rasterizer','font',{'getGlyphData':'new_glyph','hasGlyphs':'rasterizer_has_glyphs'})
aliases('font.GlyphData','font',{'clone':'glyph_clone'})
aliases('image.Image','image',{'newImageData':'new_data','newCompressedData':'decode_compressed','newCubeFaces':'cube_faces'})
aliases('image.ImageData','image',{'mapPixel':'map_pixels','paste':'paste'})
aliases('image.CompressedImageData','image',{'clone':'compressed_clone','setLinear':'compressed_set_linear'})
aliases('joystick.JoystickModule','joystick',{'getJoysticks':'get','getJoystickCount':'count','hasBackgroundEvents':'background_events','loadGamepadMappings':'load_mappings','saveGamepadMappings':'save_mappings'})
aliases('joystick.Joystick','joystick',{'getAxis':'axis','getHat':'hat','getGamepadAxis':'gamepad_axis','isGamepadDown':'gamepad_down','getGamepadMapping':'mapping','isVibrationSupported':'vibration_supported','setVibration':'vibrate','isSensorEnabled':'sensor_enabled','getSensorData':'sensor_data'})
aliases('keyboard.Keyboard','keyboard',{'getScancodeFromKey':'scancode_from_key','getKeyFromScancode':'key_from_scancode'})
aliases('mouse.Mouse','mouse',{'getSystemCursor':'system_cursor','getGlobalPosition':'global_position','getX':'position','getY':'position','setX':'set_position','setY':'set_position'})
aliases('math.Math','math',{'newRandomGenerator':'new_random','_getRandomGenerator':'new_random','newBezierCurve':'new_curve','simplexNoise':'noise'})
aliases('math.BezierCurve','math',{'getDegree':'curve_degree','getDerivative':'curve_derivative','getControlPoint':'curve_point','setControlPoint':'curve_set_point','insertControlPoint':'curve_insert','removeControlPoint':'curve_remove','getControlPointCount':'curve_count','translate':'curve_translate','rotate':'curve_rotate','scale':'curve_scale','evaluate':'curve_evaluate','getSegment':'curve_segment','render':'curve_render','renderSegment':'curve_render_segment'})
aliases('math.RandomGenerator','math',{'_random':'random','randomNormal':'random_normal','setSeed':'random_set_seed','getSeed':'random_seed','setState':'random_set_state','getState':'random_state'})
aliases('math.Transform','math',{'clone':'transform_clone','inverse':'transform_inverse','apply':'transform_apply','translate':'transform_translate','rotate':'transform_rotate','scale':'transform_scale','shear':'transform_shear','reset':'transform_reset','setMatrix':'transform_set_matrix','getMatrix':'transform_matrix','transformPoint':'transform_point','inverseTransformPoint':'transform_inverse_point','__mul':'transform_apply'})
aliases('sound.Sound','sound',{'newSoundData':'new_data'})
aliases('sound.SoundData','sound',{'copyFrom':'copy_data','slice':'slice_data'})
aliases('sound.Decoder','sound',{'clone':'decoder_clone','seek':'decoder_seek','decode':'decode_next'})
aliases('audio.Audio','audio',{'newQueueableSource':'new_queue','setVolume':'set_master_volume','getVolume':'master_volume','getActiveSourceCount':'active_count','setPosition':'set_listener_position','getPosition':'listener_position','setVelocity':'set_listener_velocity','getVelocity':'listener_velocity','setOrientation':'set_listener_orientation','getOrientation':'listener_orientation','setDopplerScale':'set_doppler','getDopplerScale':'doppler','getRecordingDevices':'recording_names','getActiveEffects':'effects','getMaxSceneEffects':'effects_info','getMaxSourceEffects':'effects_info','isEffectsSupported':'effects_info','getOutputSpatialization':'spatialization','setOutputSpatialization':'set_spatialization','getOutputSpatializationFilters':'spatialization_filters'})
aliases('audio.Source','audio',{'getDuration':'duration','getType':'source_type','getFreeBufferCount':'free_buffers','setAttenuationDistances':'set_attenuation','getAttenuationDistances':'source_info','getVolumeLimits':'source_info','getAirAbsorption':'source_info','getActiveEffects':'source_effects','setEffect':'attach_effect','getEffect':'get_source_effect'})
aliases('audio.RecordingDevice','audio',{'start':'recording_start','stop':'recording_stop','getData':'recording_data'})
aliases('sensor.Sensor','sensor',{'hasSensor':'has','getData':'data','getName':'name'})
map_methods('touch.Touch','love_touch.touches','getTouches getPosition getPressure getDeviceType isMouse','Copied touch records instead of separate object getters.')
aliases('video.Video','video',{'newVideoStream':'new'})
aliases('video.VideoStream','video',{'rewind':'seek'})
aliases('graphics.Video','video',{'getStream':'new','getSource':'source','_setSource':'set_source','getWidth':'dimensions','getHeight':'dimensions','getDimensions':'dimensions','getPixelWidth':'dimensions','getPixelHeight':'dimensions','getPixelDimensions':'dimensions','setFilter':'set_filter','getFilter':'filter'})
map_methods('video.VideoStream','','getFilename','Streams are made from copied encoded bytes, not ambient paths.','Replacement')
map_methods('video.VideoStream','love_video.set_source','setSync','External source synchronization is explicit; no Lua File-backed stream controller.')


# Consolidated destinations found during the source review.
aliases('font.Font','font',{'newRasterizer':'new_rasterizer','newBMFontRasterizer':'new_bmfont_rasterizer','newImageRasterizer':'new_image_rasterizer'})
aliases('keyboard.Keyboard','keyboard',{'isScreenKeyboardVisible':'screen_keyboard_visible'})
aliases('window.Window','window',{'setVSync':'set_vsync','getVSync':'vsync','isDisplaySleepEnabled':'display_sleep_enabled'})
aliases('math.Transform','math',{'inverseOf':'transform_inverse_of','isAffine2DTransform':'transform_is_affine','setTransformation':'transform_configure'})
map_methods('audio.Audio','','setMeter getMeter','Upstream functions are compatibility no-ops; spatial coordinates use the existing engine convention.','Excluded')
map_methods('joystick.JoystickModule','love_joystick.load_mappings','setGamepadMapping','Load SDL mapping text rather than mutate a Lua mapping tuple.')
map_methods('joystick.JoystickModule','love_joystick.info love_joystick.save_mappings','getGamepadMappingString','Get the connected device mapping or export the mapping database.')
map_methods('graphics.Graphics','love_video.new','_newVideo','Encoded bytes and an explicit video object replace the internal Lua constructor.')
map_methods('graphics.Graphics','love_graphics.new_shader','validateShader','Compile in pcall and release the result; no validation-only overload.','Replacement')
map_methods('graphics.Graphics','love_graphics.texture_format_supported','getTextureTypes','Query renderer support for each texture type/format; no bulk table.','Replacement')
map_methods('graphics.Graphics','love_graphics.pipeline_state','getColorMask getBlendState getMeshCullMode getFrontFaceWinding isGammaCorrect isLowPowerPreferred getStencilState','Pipeline snapshot; color mask is a vec4 of zero/one values.')
map_methods('graphics.Graphics','love_graphics.configure_pipeline','setBlendState setMeshCullMode setFrontFaceWinding','Checked blend/cull/winding settings record.')
map_methods('graphics.Graphics','love_graphics.default_sampler','getDefaultFilter getDefaultMipmapFilter','Default sampler snapshot.')
map_methods('graphics.Graphics','love_graphics.set_default_sampler','setDefaultFilter setDefaultMipmapFilter','Checked default sampler settings.')
aliases('graphics.Graphics','graphics',{'getShader':'current_shader','flushBatch':'flush','getCanvas':'render_target'})
aliases('graphics.Shader','graphics',{'getDebugName':'shader_debug_name'})
map_methods('physics.Joint','love_physics.joint_info','getType getAnchors getCollideConnected','Joint metadata and anchors in a record.')
map_methods('physics.Joint','love_physics.joint_reaction','getReactionForce getReactionTorque','vec3 contains force xy and torque z; pass frame dt.')
map_methods('physics.Physics','love_physics.new_body','newCircleBody newRectangleBody newPolygonBody newEdgeBody newChainBody','Create the body, then attach the corresponding collider; no combined-constructor overload.','Replacement')
aliases('physics.Physics','physics',{'newCircleShape':'new_circle','newRectangleShape':'new_box','newPolygonShape':'new_polygon','newEdgeShape':'new_edge','newChainShape':'new_chain','getDistance':'collider_distance'})
map_methods('physics.Physics','love_physics.new_circle love_physics.new_box love_physics.new_polygon love_physics.new_edge love_physics.new_chain','newFixture','Colliders are attached at construction; no separate unattached Shape/Fixture lifecycle.','Replacement')
map_methods('physics.World','love_physics.world_set_callbacks_enabled','setCallbacks','Define named global contact callbacks; the world selects delivery. No Lua closure registration.')
map_methods('physics.World','','getCallbacks','Game owns the named callback functions; host does not return a callback tuple.','Replacement')
map_methods('physics.World','love_physics.world_configure','setContactFilter setSleepingAllowed','Configure filter callback name / sleeping policy.')
map_methods('physics.World','love_physics.world_settings','getContactFilter isSleepingAllowed','World settings snapshot.')
map_methods('physics.Body','love_physics.body_is_touching','isTouching','Checks touching contacts between body_a and body_b in the same world.')
aliases('physics.World','physics',{'translateOrigin':'world_shift_origin','getBodyCount':'world_stats','getJointCount':'world_stats','getContactCount':'world_stats','queryShapesInArea':'world_query_each','getShapesInArea':'world_query','queryBoundingBox':'world_query','rayCast':'world_raycast_each','rayCastAny':'world_raycast','rayCastClosest':'world_raycast'})

map_methods('graphics.SpriteBatch','love_graphics.batch_attach_attribute','attachAttribute','Attach a buffer member; obtain a mesh-owned buffer with mesh_vertex_buffer.')
map_methods('physics.Physics','love_physics.spring_parameters','computeLinearStiffness computeLinearFrequency computeAngularStiffness computeAngularFrequency','Settings select forward/inverse conversion and angular mode; angular coefficients use pixel-squared units.')

map_methods('graphics.Graphics','love_graphics.new_font love_graphics.set_font','setNewFont','Construct the font and set it explicitly; no combined overload.')
map_methods('font.Font','love_font.new_glyph love_font.new_glyph_codepoint','newGlyphData','Glyph creation takes an existing rasterizer and text/codepoint.')
map_methods('image.Image','love_image.new_data love_image.decode','newImageData','Allocate from dimensions/raw data or decode copied encoded bytes explicitly.')
map_methods('graphics.Video','love_video.dimensions','getStream','SCUA uses a single managed video object; there is no separate exposed stream handle.','Replacement')

# Source-reviewed per-method fields replace class-wide family defaults.
from method_migrations import install
install(map_methods, ROOT)
map_methods('graphics.Shader','love_graphics.shader_send_color','sendColor',
            'Clamp color channels; convert RGB to linear when gamma correction is enabled. Accepts one color or an array.')
map_methods('graphics.Graphics','love_graphics.capture_screenshot love_graphics.screenshot_results screenshots.save','captureScreenshot',
            'Callback or polled ImageData after presentation; save encoded bytes through granted filesystem access. Actor channels use copied bytes/ids.')


def facade_exports(module, seen=()):
    if module in seen: return set()
    path=ROOT/'scua/lib'/(module+'.scua')
    source=path.read_text()
    functions=set(re.findall(r'^fn (\w+)\(',source,re.M))
    tail=source[source.rfind('return {'):]
    out=set()
    for name,value in re.findall(r'(\w+)\s*=\s*([\w.]+)',tail):
        if value in functions: out.add(module+'.'+name)
        elif '.' in value:
            other,member=value.split('.',1)
            if value in facade_exports(other,(*seen,module)):out.add(module+'.'+name)
    return out


DESTINATIONS=NATIVE | set().union(*(facade_exports(module) for module in ('filesystem','file_stream','file_data','screenshots')))

def snake(name):
    return re.sub(r'([a-z0-9])([A-Z])',r'\1_\2',re.sub(r'([A-Z]+)([A-Z][a-z])',r'\1_\2',name)).lower().lstrip('_')


def disposition(group, method):
    if (group, method) in OVERRIDES:
        return OVERRIDES[group, method]
    module, cls = group.split('.')
    prefix = {'Body':'body_', 'World':'world_', 'Joint':'joint_', 'ParticleSystem':'particles_',
              'ImageData':'', 'SoundData':'', 'Source':'', 'VideoStream':''}.get(cls, '')
    name = snake(method)
    candidates = [prefix+name]
    if name.startswith('get_'):
        candidates.append(prefix+name[4:])
    if cls == 'World' and method == 'update': candidates.insert(0,'world_step')
    if cls == 'Body' and method == 'getLinearVelocity': candidates.insert(0,'body_velocity')
    # Only modules with matching object context are eligible for automatic names.
    if group in MODULE_GROUPS or cls in OBJECT_PARAMS:
        for candidate in candidates:
            target = f'love_{module}.{candidate}'
            if target in NATIVE:
                # A same-named global function is not an object method. This used
                # to map Texture:getDimensions to the WINDOW dimensions function.
                first=NATIVE_PARAMS[target].split(',',1)[0].split(':',1)[0].strip()
                if cls in OBJECT_PARAMS and first not in OBJECT_PARAMS[cls]:
                    continue
                return ('Named', target, 'SCUA signature; port argument shapes and zero-based indices explicitly.')
    return ('Gap', '', 'No reviewed mapping in this host. Do not assume compatibility.')


def render():
    sections=[]
    counts=Counter()
    seen=set()
    for path in sorted((ROOT/'src/modules').glob('**/wrap_*.cpp')):
        module=path.relative_to(ROOT/'src/modules').parts[0]
        cls=path.stem.removeprefix('wrap_')
        group=f'{module}.{cls}'
        rows=[]
        for table, body in re.findall(r'(?:static\s+)?(?:const\s+)?luaL_Reg\s+(\w+)\s*\[\s*\]\s*=\s*\{(.*?)\};',path.read_text(),re.S):
            for method, native in re.findall(r'\{\s*"([^"]+)"\s*,\s*(\w+)\s*\}',body):
                key=(str(path),table,method)
                assert key not in seen,key
                seen.add(key)
                status,target,note=disposition(group,method)
                for destination in target.split():
                    assert destination in DESTINATIONS,(group,method,destination)
                counts[status]+=1
                destinations=', '.join(f'`{v}`' for v in target.split()) or '—'
                rows.append(f'| `{method}` | {status} | {destinations} | {note} |')
        if rows:
            sections.append(f'## {group}\n\nSource: [{path.name}]({path.relative_to(ROOT)}).\n\n| Lua entry | Disposition | SCUA destination | Migration note |\n| --- | --- | --- | --- |\n'+'\n'.join(rows))
    return ('# Upstream method migration audit\n\n'
            'Generated by `python3 scua/compatibility_audit.py` from the Lua registration tables in this fork. '
            f'Lists **{len(seen)} registered entries in {len(sections)} wrapper files**, including legacy aliases and internal functions. '
            'Inherited methods are listed at their declaring wrapper. This is a migration index, not a compatibility percentage.\n\n'
            '**Named/Adapted** identify a registered destination; signatures and defaults can differ. '
            '**Record** names source-reviewed fields with implementation links; the generator checks those keys in the named function. This is not a behavioral parity certificate. '
            '**Replacement/Host-owned/Excluded** describe deliberate boundaries. **Gap** is an exposed migration gap, not completed conversion. '
            'Actors replace Lua thread/channel patterns. Class-wide Family fallbacks are disabled: an unreviewed method is a Gap. No Lua source compatibility is promised.\n\n'
            '| Disposition | Entries |\n| --- | ---: |\n'+
            '\n'.join(f'| {k} | {v} |' for k,v in sorted(counts.items()))+'\n\n'+
            '\n\n'.join(sections)+'\n')


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check',action='store_true')
    args=parser.parse_args()
    result=render()
    destination=ROOT/'SCUA_COMPATIBILITY.md'
    if args.check:
        assert destination.read_text()==result,'Regenerate SCUA_COMPATIBILITY.md'
        print('PASS: upstream audit includes every registration and valid native destinations')
    else:
        destination.write_text(result)
