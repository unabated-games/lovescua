"""Explicit method/record migrations. No class-wide 'Family' coverage assumption.

Record entries name the concrete read/write keys and their implementation function.
The generator checks those keys occur in that function, and every destination exists.
This proves a source mapping, not behavioral equality of every Lua overload/default.
"""
import re


def function_body(path, name):
    code=path.read_text()
    # Mask comments/literals while balancing braces; preserve indices into original.
    masked=re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  lambda m:' '*len(m[0]),code,flags=re.S)
    found=re.search(r'\b'+re.escape(name)+r'\s*\([^;{}]*\)\s*\{',masked)
    assert found,(path,name)
    start=found.end()-1
    depth=1
    end=start+1
    while depth:
        depth+=(masked[end]=='{')-(masked[end]=='}')
        end+=1
    return code[start:end],code[:start].count('\n')+1


def install(map_methods, root):
    def direct(group,target,methods,note='',status='Adapted'):
        map_methods(group,target,methods,note,status)
    def record(group,target,source,function,fields,write=False,note=''):
        body,line=function_body(root/'scua'/source,function)
        for method,keys in fields.items():
            for key in keys.split():
                assert '"'+key.split('.')[0]+'"' in body,(group,method,key,source,function)
            verb='Set' if write else 'Read'
            text=f'{verb} record field(s) '+', '.join('`'+k+'`' for k in keys.split())+'. '
            text+=note+f' Source: [{function}](scua/{source}#L{line}).'
            direct(group,target,method,text,'Record')
    def language(group,methods,note):
        direct(group,'',methods,note,'Replacement')

    record('graphics.Font','love_graphics.font_metrics','objects.cpp','font_metrics',{
        'getHeight':'height','getLineHeight':'line_height','getAscent':'ascent','getDescent':'descent',
        'getBaseline':'baseline','getDPIScale':'dpi_scale'})
    record('font.Rasterizer','love_font.rasterizer_info','fonts.cpp','rasterizer_info',{
        'getHeight':'height','getAdvance':'advance','getAscent':'ascent','getDescent':'descent',
        'getLineHeight':'line_height','getGlyphCount':'glyphs'})
    record('font.GlyphData','love_font.glyph_info','fonts.cpp','glyph_info',{
        'getWidth':'width','getHeight':'height','getDimensions':'width height','getGlyph':'codepoint',
        'getGlyphString':'character','getAdvance':'advance','getBearing':'bearing_x bearing_y',
        'getBoundingBox':'min_x min_y max_x max_y','getFormat':'format'})
    record('graphics.Texture','love_graphics.texture_info','textures.cpp','texture_info',{
        'getTextureType':'type','getDimensions':'width height','getPixelDimensions':'pixel_width pixel_height','getWidth':'width','getHeight':'height','getDepth':'depth',
        'getLayerCount':'layer_count','getMipmapCount':'mipmaps','getPixelWidth':'pixel_width',
        'getPixelHeight':'pixel_height','getDPIScale':'dpi_scale','isFormatLinear':'linear',
        'isCompressed':'compressed','getMSAA':'msaa','getFormat':'format','isCanvas':'render_target',
        'isComputeWritable':'compute_write','isReadable':'readable','getViewFormats':'view_formats',
        'getMipmapMode':'mipmaps_mode','getDebugName':'debug_name'},note='Optional mip is zero-based. ')
    record('graphics.Texture','love_graphics.texture_sampler','textures.cpp','texture_sampler',{
        'getFilter':'min_filter mag_filter anisotropy','getMipmapFilter':'mipmap_filter lod_bias',
        'getWrap':'wrap_u wrap_v wrap_w','getDepthSampleMode':'depth_compare'})
    record('graphics.Quad','love_graphics.quad_info','drawables.cpp','quad_info',{
        'getViewport':'position size','getTextureDimensions':'texture_dimensions','getLayer':'layer'},
        note='Layer is zero-based; quads retain their source texture. Out-of-texture viewports support wrapping. ')
    direct('graphics.Quad','love_graphics.quad_set_layer','setLayer','Zero-based layer; bounded by the retained source array texture.')
    record('graphics.Buffer','love_graphics.buffer_info','buffers.cpp','buffer_info',{
        'getElementCount':'length','getElementStride':'stride','getSize':'bytes','getFormat':'format',
        'isBufferType':'usages','getDebugName':'debug_name'},note='isBufferType tests membership in usages. ')
    record('graphics.Mesh','love_graphics.mesh_info','graphics_extra.cpp','mesh_info',{
        'getVertexCount':'count','getVertexFormat':'format','isAttributeEnabled':'format attributes',
        'getAttachedAttributes':'attributes','getVertexMap':'vertex_map','getDrawMode':'mode','getDrawRange':'range'},
        note='Zero-based ranges/indices. Attribute enabled is on each format/attached-attribute entry. ')
    record('graphics.SpriteBatch','love_graphics.batch_info','graphics_extra.cpp','batch_info',{
        'getColor':'color','getCount':'count','getBufferSize':'capacity','getDrawRange':'range'})
    record('image.ImageData','love_image.info','image_extra.cpp','image_info',{
        'getFormat':'format','isLinear':'linear','getWidth':'width','getHeight':'height'})
    record('image.CompressedImageData','love_image.compressed_info','textures.cpp','compressed_info',{
        'getWidth':'width','getHeight':'height','getDimensions':'width height','getMipmapCount':'mipmaps',
        'getFormat':'format','isLinear':'linear'},note='Optional mip is zero-based. ')
    record('audio.RecordingDevice','love_audio.recording_info','audio.cpp','recording_info',{
        'getSampleCount':'samples','getSampleRate':'sample_rate','getBitDepth':'bit_depth',
        'getChannelCount':'channels','getName':'name','isRecording':'recording'})
    record('audio.Source','love_audio.source_info','audio_controls.cpp','source_info',{'getRolloff':'rolloff','getAirAbsorption':'air_absorption','getAttenuationDistances':'attenuation','getVolumeLimits':'volume_limits'},
        note='Spatial fields require a mono source. ')
    record('sound.SoundData','love_sound.info','audio.cpp','love_sound_info',{
        'getChannelCount':'channels','getBitDepth':'bit_depth','getSampleRate':'sample_rate',
        'getSampleCount':'samples','getDuration':'duration'})
    record('sound.Decoder','love_sound.decoder_info','audio.cpp','decoder_info',{
        'getChannelCount':'channels','getBitDepth':'bit_depth','getSampleRate':'sample_rate','getDuration':'duration'})
    record('joystick.Joystick','love_joystick.info','input.cpp','info',{
        'isConnected':'connected','getName':'name','getID':'id instance_id','getGUID':'guid',
        'getDeviceInfo':'vendor product product_version','getDevicePowerInfo':'power battery_percent',
        'getDeviceConnectionState':'connection','getJoystickType':'joystick_type',
        'getAxisCount':'axes','getButtonCount':'buttons','getHatCount':'hats','getAxes':'axis_values',
        'getPlayerIndex':'player_index','isGamepad':'gamepad','getGamepadType':'gamepad_type',
        'getGamepadMappingString':'mapping','getVibration':'vibration','getConnectedIndex':'connected_index'},
        note='SCUA indices are zero-based. ')
    direct('joystick.Joystick','love_joystick.is_down','isDown','Call per zero-based button index; combine checks for several buttons.')
    direct('joystick.Joystick','love_joystick.set_player_index','setPlayerIndex')
    direct('joystick.Joystick','love_joystick.has_sensor','hasSensor')
    direct('joystick.Joystick','love_joystick.set_sensor_enabled','setSensorEnabled')
    direct('mouse.Cursor','love_mouse.cursor_info','getType','Read kind and optional system fields.')

    particle_fields={'getBufferSize':'capacity','getInsertMode':'insert_mode','getEmissionRate':'rate',
        'getEmitterLifetime':'emitter_lifetime','getParticleLifetime':'lifetime','getPosition':'position',
        'getEmissionArea':'area','getDirection':'direction','getSpread':'spread','getSpeed':'speed',
        'getLinearAcceleration':'acceleration_min acceleration_max','getRadialAcceleration':'radial_acceleration',
        'getTangentialAcceleration':'tangential_acceleration','getLinearDamping':'damping','getSizes':'sizes',
        'getSizeVariation':'size_variation','getRotation':'rotation','getSpin':'spin',
        'getSpinVariation':'spin_variation','getColors':'colors','getOffset':'offset',
        'hasRelativeRotation':'relative_rotation','isPaused':'paused','isStopped':'stopped'}
    record('graphics.ParticleSystem','love_graphics.particles_state','particles.cpp','particles_state',particle_fields,
        note='Ranges are vec2; acceleration bounds are vec2; area is a nested settings record. ')
    particle_setters={'setBufferSize':'capacity','setInsertMode':'insert_mode','setEmissionRate':'rate',
        'setParticleLifetime':'lifetime','setEmissionArea':'area','setLinearAcceleration':'acceleration_min acceleration_max',
        'setSizes':'sizes','setLinearDamping':'damping','setSpinVariation':'spin_variation','setRelativeRotation':'relative_rotation'}
    record('graphics.ParticleSystem','love_graphics.particles_configure','particles.cpp','particles_configure',particle_setters,True)

    body_fields={'getX':'position.x','getY':'position.y','getTransform':'position angle',
        'getWorldCenter':'center','getLocalCenter':'local_center',
        'getKinematicState':'position angle velocity angular_velocity','getInertia':'inertia',
        'hasCustomMassData':'custom_mass','getAngularDamping':'angular_damping','getLinearDamping':'linear_damping',
        'getGravityScale':'gravity_scale','isBullet':'bullet','isActive':'enabled',
        'isSleepingAllowed':'sleeping_allowed','isFixedRotation':'fixed_rotation'}
    record('physics.Body','love_physics.body_info','physics.cpp','body_info',body_fields)
    record('physics.Body','love_physics.body_configure','physics_extra.inc','body_configure',{
        'setKinematicState':'position angle velocity angular_velocity','setMass':'mass','setInertia':'inertia',
        'setAngularDamping':'angular_damping','setLinearDamping':'linear_damping'},True,
        note='All supplied fields validated before any mutation; omitted fields preserved. ')
    direct('physics.Body','love_physics.body_position love_physics.body_set_position','setX setY',
        'Read position, replace one component, set vec2. Other component is preserved explicitly.')
    direct('physics.Body','love_physics.body_set_velocity','setLinearVelocity','Use vec2.')
    direct('physics.Body','love_physics.body_apply_impulse love_physics.body_apply_impulse_at','applyLinearImpulse',
        'Choose center impulse or explicit world-point impulse.')
    direct('physics.Body','love_physics.body_transform_points','getWorldPoints getLocalPoints',
        'Array of vec2; local=false maps to world, local=true maps to body coordinates.')
    direct('physics.Body','love_physics.body_velocity_at_point','getLinearVelocityFromWorldPoint')
    direct('physics.Body','love_physics.body_local_velocity','getLinearVelocityFromLocalPoint')
    direct('physics.Body','love_physics.body_set_enabled','setActive')
    direct('physics.Body','love_physics.body_colliders','getShape getShapes getFixtures',
        'Returns attached-collider array; select the first for the single-shape convenience method.')
    direct('physics.Body','love_physics.body_contact_list','getContacts')

    shape_fields={'getType':'kind','getRadius':'radius','getChildCount':'children','getFriction':'friction',
        'getRestitution':'restitution','getDensity':'density','isSensor':'sensor',
        'getFilterData':'category mask group','getCategory':'category','getMask':'mask','getGroupIndex':'group'}
    record('physics.Shape','love_physics.collider_shape','physics_extra.inc','shapeSnapshot',shape_fields,
        note='Category and mask are 16-bit bitsets; mask denotes ALLOWED categories, not Lua setMask excluded indices. ')
    record('physics.Shape','love_physics.collider_configure','physics_extra.inc','collider_configure',{
        'setFriction':'friction','setRestitution':'restitution','setDensity':'density','setSensor':'sensor',
        'setFilterData':'category mask group','setCategory':'category','setMask':'mask','setGroupIndex':'group'},True,
        note='Convert category indices to bits. For Lua setMask, invert the excluded bits within 16 bits. ')
    for methods,target in [('getBody','collider_body'),('testPoint','collider_test_point'),('rayCast','collider_raycast'),
        ('computeAABB','collider_compute_bounds'),('computeMass getMassData','collider_mass'),
        ('getBoundingBox','collider_bounds'),('destroy','collider_destroy'),('isDestroyed','collider_is_destroyed')]:
        direct('physics.Shape','love_physics.'+target,methods,'Attached collider; child indices are zero-based.')
    language('physics.Shape','getShape','The collider already combines the fixture and its shape; use the same handle.')
    for cls in ('CircleShape','EdgeShape','ChainShape','PolygonShape'):
        fields={'getPoints':'points'} if cls!='CircleShape' else {'getRadius':'radius','getPoint':'center'}
        if cls in ('EdgeShape','ChainShape'):fields.update(getNextVertex='next',getPreviousVertex='previous')
        if cls=='ChainShape':fields.update(getVertexCount='points',getPoint='points')
        if cls=='PolygonShape':fields['validate']='valid'
        record('physics.'+cls,'love_physics.collider_shape','physics_extra.inc','shapeSnapshot',fields,
            note='Points are vec2 arrays; chain point indices are zero-based. ')
        if cls in ('EdgeShape','ChainShape'):
            record('physics.'+cls,'love_physics.collider_configure','physics_extra.inc','collider_configure',
                {'setNextVertex':'next','setPreviousVertex':'previous'},True)
        if cls=='CircleShape':
            record('physics.'+cls,'love_physics.collider_configure','physics_extra.inc','collider_configure',
                {'setRadius':'radius','setPoint':'center'},True)
    direct('physics.ChainShape','love_physics.collider_child_edge','getChildEdge','Returns a child-edge record; zero-based index.')

    record('physics.Contact','love_physics.contact_info','physics_extra.inc','contactSnapshot',{
        'getPositions':'points','getNormal':'normal','getFriction':'friction','getRestitution':'restitution',
        'isEnabled':'enabled','isTouching':'touching','getTangentSpeed':'tangent_speed','getChildren':'child_a child_b',
        'getShapes':'collider_a collider_b','getFixtures':'collider_a collider_b'},
        note='Use world+contact id. Collider IDs resolve through world_collider; child indices are zero-based. ')
    direct('physics.Contact','love_physics.contact_set','setFriction setRestitution setEnabled setTangentSpeed',
        'Set respectively friction/restitution/enabled/tangent_speed in the settings record.')
    direct('physics.Contact','love_physics.contact_reset','resetFriction resetRestitution',
        'Pass field="friction" or "restitution" to reset the selected property.')
    direct('physics.Contact','love_physics.contact_info','isDestroyed','A retired contact id returns nil; no native Contact userdata.')

    joint_fields={
      'Distance':{'getLength':'length','getStiffness':'stiffness','getDamping':'damping'},
      'Friction':{'getMaxForce':'max_force','getMaxTorque':'max_torque'},
      'Gear':{'getRatio':'ratio','getJoints':'source_a source_b'},
      'Motor':{'getLinearOffset':'linear_offset','getAngularOffset':'angular_offset','getMaxForce':'max_force',
               'getMaxTorque':'max_torque','getCorrectionFactor':'correction_factor'},
      'Mouse':{'getTarget':'target','getMaxForce':'max_force','getStiffness':'stiffness','getDamping':'damping'},
      'Pulley':{'getGroundAnchors':'ground_anchor_a ground_anchor_b','getLengthA':'length_a','getLengthB':'length_b','getRatio':'ratio'},
      'Rope':{'getMaxLength':'max_length'},
      'Weld':{'getStiffness':'stiffness','getDamping':'damping','getReferenceAngle':'reference_angle'}}
    for cls in ('Prismatic','Revolute','Wheel'):
        f={'getJointSpeed':'linear_speed' if cls=='Wheel' else 'speed','isMotorEnabled':'motor_enabled',
           'getMotorSpeed':'motor_speed','areLimitsEnabled':'limit_enabled','getLowerLimit':'lower_limit',
           'getUpperLimit':'upper_limit','getLimits':'lower_limit upper_limit'}
        if cls=='Revolute':f.update(getJointAngle='angle',getMaxMotorTorque='max_motor_torque',getMotorTorque='motor_torque',getReferenceAngle='reference_angle')
        else:f.update(getJointTranslation='translation',getAxis='axis')
        if cls=='Prismatic':f.update(getMotorForce='motor_force',getMaxMotorForce='max_motor_force',getReferenceAngle='reference_angle')
        if cls=='Wheel':f.update(getMotorTorque='motor_torque',getMaxMotorTorque='max_motor_torque',
            getSpringStiffness='stiffness',getStiffness='stiffness',getSpringDamping='damping',getDamping='damping')
        joint_fields[cls]=f
    for cls,fields in joint_fields.items():
        record('physics.'+cls+'Joint','love_physics.joint_state','physics_joints.inc','jointFields',fields,
            note='Pass dt for motor force/torque. Axis is world-space; gear source IDs resolve through world_joint. ')
        setters={name.replace('get','set',1):keys for name,keys in fields.items()
                 if name.startswith('get') and name not in ('getAxis','getReferenceAngle','getJoints','getGroundAnchors',
                 'getLengthA','getLengthB','getMotorTorque','getMotorForce','getJointTranslation','getJointSpeed','getJointAngle')}
        setters={name:keys for name,keys in setters.items() if name not in ('setSpringStiffness','setSpringDamping')}
        if cls in ('Prismatic','Revolute','Wheel'):
            setters.update(setMotorEnabled='motor_enabled',setLimitsEnabled='limit_enabled',setLimits='lower_limit upper_limit')
        if cls=='Wheel':setters.update(setSpringStiffness='stiffness',setSpringDamping='damping')
        if setters:record('physics.'+cls+'Joint','love_physics.joint_configure','physics_joints.inc','configureJoint',setters,True,
            note='Pixel/radian units; omitted fields retain their current values. ')

    # Deliberate language/lifecycle replacements, with per-method scopes.
    language('data.Data','getString','Decode bytes.to_string for UTF-8; binary content stays bytes (SCUA strings cannot contain invalid UTF-8).')
    language('data.Data','getSize','Read bytes.len() or typed-array length times element size.')
    language('data.Data','getFloat getDouble getInt8 getUInt8 getInt16 getUInt16 getInt32 getUInt32',
        'Use endian-explicit bytes.u8/u16/u32/f32/f64 readers; sign-extend integers as needed. Native-pointer layout is not the contract.')
    language('data.ByteData','clone setString setFloat setDouble setInt8 setUInt8 setInt16 setUInt16 setInt32 setUInt32',
        'Immutable bytes and mutable typed arrays replace ByteData. Build a new byte buffer or update the typed array; no in-place ByteData object.')
    language('data.DataView','clone','Use bytes.slice or a typed-array slice. No Lua DataView object or cross-partition shared mutable alias.')
    language('data.CompressedData','clone getFormat',
        'Compressed bytes are immutable and can be shared; retain the compression algorithm in an explicit game record.')
    for method in ('getSize','open','close','isOpen','read','write','flush','isEOF','tell','seek','lines','setBuffer','getBuffer','getMode','getFilename','getExtension'):
        target={'getSize':'get_size','open':'open','close':'close','isOpen':'is_open','read':'read','write':'write',
                'flush':'flush','isEOF':'is_eof','tell':'tell','seek':'seek','lines':'lines','setBuffer':'set_buffer',
                'getBuffer':'get_buffer','getMode':'get_mode','getFilename':'get_filename','getExtension':'get_extension'}[method]
        direct('filesystem.File','file_stream.'+target,method,
            'Partition-local rooted stream; read returns bytes, I/O returns Results. lines returns a next-line Result closure. Explicit close/with_file flushes buffered writes.')
    for method,target in {'clone':'clone','getFilename':'get_filename','getExtension':'get_extension'}.items():
        direct('filesystem.FileData','file_data.'+target,method,'Named immutable bytes in a SCUA record; clone copies metadata safely.')
    fs={'createDirectory':'create_directory','remove':'remove_file','read':'read','write':'write','append':'append',
        'getDirectoryItems':'get_directory_items','lines':'lines','exists':'exists','getInfo':'get_info','newFileData':'new_file_data',
        'getIdentity':'get_identity','mount':'mount','openFile':'open','newFile':'open'}
    for method,target in fs.items():
        direct('filesystem.Filesystem','filesystem.'+target,method,
            'Use res:/user: paths and Result I/O. Reads default to res:, writes default to user:.')
    direct('filesystem.Filesystem','','setIdentity getSaveDirectory getSourceBaseDirectory getExecutablePath getRequirePath setRequirePath unmount',
        'Host owns grants, identity, import paths and fixed resource layers. Configure before loading/spawning; no mutable Lua global filesystem lifecycle.','Host-owned')
    language('filesystem.Filesystem','load','Use checked static SCUA imports; loading/executing Lua source is not exposed.')
