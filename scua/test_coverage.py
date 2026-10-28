"""Behavior checks for gaps found by the source audit, without a window."""
from pathlib import Path
import subprocess
import sys
import tempfile
import compatibility_audit as audit

# New object methods must not inherit class-wide coverage or collide with globals.
for group,method in [('graphics.Texture','unreviewedMethod'),('graphics.Texture','getRendererInfo'),('graphics.Shader','getDimensions'),('graphics.NewObject','getDimensions')]:
    assert audit.disposition(group,method)[0]=='Gap',(group,method)
assert '| Family |' not in audit.render()
assert audit.disposition('graphics.Texture','getDimensions')[1]=='love_graphics.texture_info'
assert audit.disposition('graphics.Quad','getTextureDimensions')[1]=='love_graphics.quad_info'
print('PASS: unreviewed/object-global collisions remain gaps; dimension mappings use the actual resource')

host=str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix='lovescua-coverage-') as tmp:
    root=Path(tmp)
    (root/'scua.toml').write_text('[package]\nscua-version="0.38"\n')
    (root/'main.scua').write_text('''import love
fn near(a,b) return abs(a-b)<0.001 end
fn fails(body)
 match pcall(body) Error(_) -> nil Ok(_) -> assert(false,"operation should fail") end
end
fn load()
 let world=love.physics.new_world(vec2(0,0))
 let body=love.physics.new_body(world,vec2(5,6),"dynamic")
 love.physics.new_circle(body,10,1)
 love.physics.body_configure(body,{position=vec2(40,50),angle=0.7,velocity=vec2(3,4),angular_velocity=0.2,
   linear_damping=1.5,angular_damping=2.5,mass=2,inertia=150})
 let state=love.physics.body_info(body)
 assert((state.position-vec2(40,50)).length()<0.001 and near(state.angle,0.7))
 assert((state.velocity-vec2(3,4)).length()<0.001 and near(state.angular_velocity,0.2))
 assert(near(state.linear_damping,1.5) and near(state.angular_damping,2.5))
 assert(near(state.mass,2) and near(state.inertia,150) and state.custom_mass)
 fails(fn() love.physics.body_configure(body,{position=vec2(100,200),angular_damping=-1}) end)
 assert((love.physics.body_position(body)-vec2(40,50)).length()<0.001,"invalid update must be atomic")
 love.physics.body_configure(body,{mass=3})
 assert(near(love.physics.body_info(body).mass,3) and near(love.physics.body_info(body).inertia,150))
 love.physics.body_configure(body,{linear_damping=4})
 assert(near(love.physics.body_info(body).angular_damping,2.5),"omitted fields preserved")
 let anchor=love.physics.new_body(world,vec2(0,0),"static")
 love.physics.body_set_angle(anchor,1.57079632679)
 let slider=love.physics.new_prismatic_joint(anchor,body,vec2(0,0),vec2(1,0))
 let joint=love.physics.joint_state(slider)
 assert((joint.axis-vec2(1,0)).length()<0.001,"joint axis must be world-space")
 assert((joint.local_axis-vec2(0,-1)).length()<0.001,"local axis remains separately available")
 let wheel=love.physics.new_wheel_joint(anchor,body,vec2(0,0),vec2(1,0))
 assert((love.physics.joint_state(wheel).axis-vec2(1,0)).length()<0.001)
 assert(not love.system.has_background_music(),"matches upstream desktop behavior")
 print("PASS: body state/mass/damping updates, atomic failure, preserved fields, rotated joint axes and desktop music probe")
end
fn verify() end
''')
    run=subprocess.run([host,'--headless','--verify','--frames','1',str(root)],capture_output=True,text=True,timeout=10)
    assert run.returncode==0,(run.stdout,run.stderr)
    print(run.stdout.strip())
