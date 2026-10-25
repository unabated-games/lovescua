"""Real embedded frame/actor regressions, all without windows or audio devices."""
from pathlib import Path
import subprocess
import sys
import tempfile

host=str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix='lovescua-jobs-') as directory:
    root=Path(directory)
    (root/'scua.toml').write_text('[package]\nscua-version="0.38"\n')
    def run(name,source,frames=6,options=(),error=None):
        (root/'main.scua').write_text(source)
        result=subprocess.run([host,'--headless','--frames',str(frames),'--fixed-dt','0.016666666666666666','--verify',*options,str(root)],capture_output=True,text=True,timeout=12)
        if error:
            assert result.returncode!=0 and error in result.stderr,(name,result.stdout,result.stderr)
        else:
            assert result.returncode==0,(name,result.stdout,result.stderr)
        print('PASS:',name)
        return result

    run('resumable self-messaging work respects the turn limit', '''import love
partition Busy
  state receiver=nil
  state n=0
  on Init(me) receiver=me tell receiver.More() end
  on More()
    n=n+1
    love_job_commands.submit(0,n,n,0)
    tell receiver.More()
  end
end
let busy=Busy()
let count=0
fn load() tell busy.Init(busy) end
fn post_update(dt)
  count=count+love.jobs.commands().len()
  assert(love.jobs.info().more_ready and love.jobs.info().budget_hit)
  assert(love.jobs.info().turns<=2)
end
fn verify() assert(count==11,`count={count} turns={love.jobs.info().turns}`) end
''',options=('--actor-turns','2','--actor-budget-ms','0'))

    timer='''import love
let frame=0
let samples=[]
let stopped=0
spawn(fn()
  let n=0
  while true do
    love_job_commands.submit(99,n,now(),0)
    n=n+1
    wait(50ms)
  end
end)
fn load() assert(now()==0) end
fn update(dt)
  frame=frame+1
  assert(now()==love.jobs.info().clock_ms)
  if frame==2 then love.jobs.set_paused(true) stopped=now() end
  if frame==4 then assert(now()==stopped) love.jobs.set_paused(false) end
end
fn post_update(dt)
  for command in love.jobs.commands() do
    assert(command.position.x<=now())
    samples.push(command.position.x)
  end
end
fn verify()
  assert(love.jobs.info().clock_ms==66,"fractional dt must accumulate across frames")
  assert(samples.len()>=2 and samples.len()<=3,"wait must span frames, including a pause")
  for i in 1:samples.len() do assert(samples[i]-samples[i-1]>=50) end
end
'''
    run('host clock, fractional milliseconds, waits and pause',timer)
    run('tasks with multicore enabled',timer,options=('--workers','4'))

    # File-loaded names must remain intact after loading; the SDK owns its source.
    fault='''import love
partition Broken
  on Crash() assert(false,"deliberate actor fault") end
end
let broken=Broken()
let frame=0
let reports=0
fn load() tell broken.Crash() assert(love.jobs.info().fault_count==0) end
fn update(dt) frame=frame+1 end
fn post_update(dt)
  let errors=love.jobs.faults()
  reports=reports+errors.len()
  if frame==1 then assert(errors.len()==1 and errors[0].kind=="actor" and errors[0].partition=="Broken") end
end
fn verify() assert(reports==1 and frame==6 and love.jobs.info().fault_count==1) end
'''
    for workers in (0,4):
        result=run(f'fault reported once, update continues ({workers} workers)',fault,options=('--workers',str(workers)))
        assert result.stderr.count('deliberate actor fault')==1,result.stderr

    batch='''import love
partition Worker
  on Work(id)
    let snapshot=love_job_reads.read(id)
    let value=0
    for k in 0:1000 do value=value+k end
    love_job_commands.submit(id,1,snapshot.x,value)
  end
end
let workers=[]
fn load()
  for i in 0:6 do
    let worker=Worker()
    workers.push(worker)
    love.jobs.publish(i,vec4(i*10,0,0,0))
    love.jobs.set_order_key(i,5-i)
    tell worker.Work(i)
  end
end
let seen=0
fn post_update(dt)
  let commands=love.jobs.commands()
  for command in commands do
    assert(command.id==seen and command.step==1)
    assert(command.position==vec2(seen*10,499500))
    seen=seen+1
  end
end
fn verify()
  assert(seen==6 and love.jobs.info().deferred_pending==0)
  if love.jobs.info().workers>1 then assert(love.jobs.info().deferred_applied==6) end
end
'''
    for workers,order in ((0,'index'),(0,'reverse'),(4,'index'),(4,'reverse'),(4,'shuffle:7')):
        run(f'copied results and stable game-id ordering ({workers}, {order})',batch,options=('--workers',str(workers),'--actor-order',order,'--actor-budget-ms','0'))

    (root/'alias_helpers.scua').write_text('import love_job_reads as engine\nfn wrapped(id) return engine.read(id) end\nreturn {raw=engine.read,wrapped=wrapped}\n')
    (root/'imported_agent.scua').write_text('''import love_job_reads as alias
import alias_helpers as api
import jobs as bridge
partition ImportedAgent
  on Go()
    let a=alias.read(2)
    let b=api.raw(2)
    let c=api.wrapped(2)
    let d=bridge.read(2)
    bridge.submit(2,1,a.x+b.x+c.x+d.x,0)
  end
end
fn start()
  let agent=ImportedAgent()
  tell agent.Go()
  return agent
end
return {start=start}
''')
    aliases='''import love
import imported_agent as agents
let brain=nil
let done=false
fn load()
  love.jobs.publish(2,vec4(3,0,0,0))
  brain=agents.start()
end
fn post_update(dt)
  for command in love.jobs.commands() do
    assert(command.id==2 and command.position==vec2(12,0))
    done=true
  end
end
fn verify() assert(done and love.jobs.info().fault_count==0) end
'''
    for workers in (0,4):
        run(f'imported actor, native alias, raw facade and module alias ({workers})',aliases,options=('--workers',str(workers)))

    fairness='''import love
partition Worker
  on Go(id) love_job_commands.submit(id,1,id,0) end
end
let seen=[]
let count=0
fn load()
  for i in 0:10 do
    seen.push(false)
    let worker=Worker()
    tell worker.Go(i)
  end
end
fn post_update(dt)
  let commands=love.jobs.commands()
  assert(commands.len()<=3 and love.jobs.info().turns<=3)
  for command in commands do
    assert(not seen[command.id])
    seen[command.id]=true
    count=count+1
  end
end
fn verify()
  assert(count==10)
  for value in seen do assert(value) end
end
'''
    for workers in (0,4):
        run(f'low turn limit across many actors loses no messages ({workers})',fairness,
            options=('--workers',str(workers),'--actor-turns','3','--actor-budget-ms','0','--actor-order','shuffle:7'))

    run('actor userdata remains local and cleans up with workers', '''import love
partition Pixels
  on Work()
    let image=love_image.new_data(2,3)
    love_image.set_pixel(image,0,0,vec4(1,0,0,1))
    assert(love_image.get_pixel(image,0,0).x==1)
    love_object.close(image)
    love_job_commands.submit(0,1,2,3)
  end
end
let pixels=Pixels()
let done=false
fn load() tell pixels.Work() end
fn post_update(dt)
  let commands=love.jobs.commands()
  if commands.len()>0 then assert(commands[0].position==vec2(2,3)) done=true end
end
fn verify() assert(done and love.jobs.info().fault_count==0) end
''',options=('--workers','4'))

    run('time scale affects subsequent clock advances', '''import love
let frame=0
fn update(dt)
  frame=frame+1
  if frame==1 then love.jobs.set_time_scale(0.5) end
end
fn verify() assert(love.jobs.info().clock_ms==58) end
''')

    run('reentrant physics callbacks stay in the serial phase', '''import love
let world=nil
let hits=0
fn begincontact(contact) hits=hits+1 end
fn load()
  world=love.physics.new_world(vec2(0,0))
  let a=love.physics.new_body(world,vec2(0,0),"dynamic")
  let b=love.physics.new_body(world,vec2(10,0),"dynamic")
  love.physics.new_circle(a,10,1)
  love.physics.new_circle(b,10,1)
end
fn post_update(dt) love.physics.world_step(world,dt) end
fn verify() assert(hits>0) end
''',options=('--workers','4'))

    run('reentrant engine operations rejected during actor work', '''import love
partition Unsafe
  on Go()
    let world=love_physics.new_world(vec2(0,0))
    love_physics.world_step(world,0.01)
  end
end
let unsafe=Unsafe()
let faults=[]
fn load() tell unsafe.Go() end
fn post_update(dt)
  for fault in love.jobs.faults() do faults.push(fault) end
end
fn verify()
  assert(faults.len()==1 and love.jobs.info().fault_count==1)
  assert(faults[0].message!="")
end
''',options=('--workers','4'))

    run('actor reload preserves logical state and clock, rebuilds workers', '''import love
import json
partition Brain
  on Go(step) love_job_commands.submit(0,step,now(),0) end
end
let brain=Brain()
let tick=0
let reloads=0
let replies=0
fn snapshot() return json.encode({tick=tick,reloads=reloads}) end
fn restore(encoded)
  match json.decode(encoded)
    Ok(saved) -> do tick=saved.tick reloads=saved.reloads+1 end
    Error(reason) -> assert(false,tostring(reason))
  end
end
fn update(dt)
  tick=tick+1
  tell brain.Go(tick)
  if tick==2 then love.event.reload() end
end
fn post_update(dt) replies=replies+love.jobs.commands().len() end
fn verify()
  assert(tick==6 and reloads==1 and replies>0)
  assert(love.jobs.info().clock_ms==100 and love.jobs.info().fault_count==0)
end
''',options=('--workers','4'))

    runaway='''import love
partition Spin
  on Go() while true do end end
  on Ping() love_job_commands.submit(0,1,42,0) end
end
partition Fine
  on Go() love_job_commands.submit(1,1,99,0) end
end
let spin=Spin()
let fine=Fine()
let reports=[]
let saw_spin=false
let saw_fine=false
let frame=0
fn load() tell spin.Go() tell fine.Go() end
fn update(dt) frame=frame+1 end
fn post_update(dt)
  for fault in love.jobs.faults() do
    assert(fault.kind=="actor" and fault.partition=="Spin" and fault.actor_slot==0)
    assert(fault.actor_generation>=0 and fault.message!="")
    reports.push(fault)
    tell spin.Ping()
  end
  for command in love.jobs.commands() do
    if command.id==0 then assert(command.position.x==42) saw_spin=true end
    if command.id==1 then assert(command.position.x==99) saw_fine=true end
  end
end
fn verify()
  assert(frame==16 and reports.len()==1 and love.jobs.info().fault_count==1)
  assert(reports[0].partition=="Spin" and reports[0].message!="","fault strings survive later drains")
  assert(saw_spin and saw_fine and not love.jobs.info().paused)
end
'''
    for workers in (0,4):
        run(f'runaway actor isolated; healthy actor and next message continue ({workers})',runaway,
            frames=16,options=('--workers',str(workers),'--actor-turns','128','--actor-budget-ms','0'))

    returned_error='''import love
partition NoAmmo
  on Go() return Error("no ammo") end
  on Ping() love_job_commands.submit(0,1,17,0) end
end
let actor=NoAmmo()
let seen=[]
let resumed=false
fn load() tell actor.Go() end
fn post_update(dt)
  for fault in love.jobs.faults() do
    assert(fault.kind=="error" and fault.partition=="NoAmmo" and fault.message.len()>0)
    seen.push(fault)
    tell actor.Ping()
  end
  if love.jobs.commands().len()>0 then resumed=true end
end
fn verify()
  assert(seen.len()==1 and seen[0].message.len()>0 and resumed)
  assert(love.jobs.info().fault_count==1)
end
'''
    bad_text='''import love
partition Tagged
  on Go() love_job_commands.submit(0,1,2,3) end
end
let actor=Tagged()
let seen=[]
let completed=false
fn load()
  assert(love.jobs.post_text(0,"wrong message type"))
  tell actor.Go()
end
fn post_update(dt)
  for fault in love.jobs.faults() do
    assert(fault.kind=="actor" and fault.partition=="Tagged" and fault.message!="")
    seen.push(fault)
  end
  if love.jobs.commands().len()>0 then completed=true end
end
fn verify() assert(seen.len()==1 and seen[0].partition=="Tagged" and completed) end
'''
    for workers in (0,4):
        options=('--workers',str(workers))
        result=run(f'unanswered Error has its own once-only fault kind ({workers})',returned_error,options=options)
        assert result.stderr.count('no ammo')==1,result.stderr
        run(f'posted text to a tagged partition faults visibly ({workers})',bad_text,options=options)

    run('deferred validation faults surface once in the serial phase', '''import love
partition Invalid
  on Go() love_job_commands.submit(-1,1,0,0) end
end
let actor=Invalid()
let seen=0
fn load() tell actor.Go() end
fn post_update(dt) seen=seen+love.jobs.faults().len() end
fn verify() assert(seen==1 and love.jobs.info().fault_count==1) end
''',options=('--workers','4'))

    for flag,value in (('--workers','65'),('--workers','-1'),('--actor-budget-ms','-1'),('--actor-order','invalid')):
        run('invalid scheduler option '+flag,'fn verify() end',options=(flag,value),error='lovescua:')
    run('unbounded scheduler refused','fn verify() end',options=('--actor-budget-ms','0','--actor-turns','0'),error='time or turn limit')
