"""Real rooted streams: buffering, seeking, bounded memory, resource packs and cleanup."""
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

host=str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix='lovescua-streams-') as tmp:
    root=Path(tmp)
    (root/'res').mkdir()
    (root/'saves').mkdir()
    (root/'res/input.bin').write_bytes(b'hello\r\n\r\nworld\nlast')
    (root/'res/invalid.bin').write_bytes(b'\xff\nvalid\n')
    (root/'res/long.txt').write_bytes(b'x'*65536+b'\n')
    (root/'outside').mkdir()
    (root/'outside/keep.txt').write_text('outside grant')
    (root/'saves/escape').symlink_to(root/'outside',target_is_directory=True)
    (root/'scua.toml').write_text('[package]\nscua-version="0.38"\n')
    (root/'main.scua').write_text('''import filesystem as filesystem
import fs
import str
import bytes
let f=filesystem.stream
fn must(result)
  match result
    Ok(value) -> return value
    Error(reason) -> assert(false,tostring(reason))
  end
end
fn fails(body)
  match pcall(body)
    Error(_) -> nil
    Ok(_) -> assert(false,"operation should fault")
  end
end
fn load()
  let reader=must(f.open("input.bin"))
  assert(f.get_filename(reader)=="res:input.bin" and f.get_extension(reader)=="bin")
  assert(f.is_open(reader) and f.get_mode(reader)=="r")
  assert(must(f.read_line(reader))=="hello" and f.tell(reader)==7)
  assert(must(f.read_line(reader))=="" and f.tell(reader)==9)
  assert(must(f.get_size(reader))==19 and f.tell(reader)==9)
  must(f.set_buffer(reader,"full",5))
  assert(bytes.to_string(must(f.read(reader,2)))==Ok("wo"))
  assert(f.tell(reader)==11 and not f.is_eof(reader))
  must(f.seek(reader,-2,"current"))
  assert(f.tell(reader)==9)
  assert(must(f.read_line(reader))=="world")
  assert(must(f.read_line(reader))=="last")
  assert(f.is_eof(reader))
  assert(must(f.read(reader,5)).len()==0)
  must(f.seek(reader,0))
  assert(not f.is_eof(reader) and bytes.to_string(must(f.read(reader,5)))==Ok("hello"))
  must(f.close(reader))
  must(f.close(reader))
  assert(not f.is_open(reader) and f.get_mode(reader)=="closed")
  fails(fn() f.read(reader,1) end)
  let iter=must(f.open("input.bin"))
  let next=f.lines(iter)
  assert(must(next())=="hello" and must(next())=="" and must(next())=="world")
  assert(must(next())=="last" and must(next())==nil)
  must(f.close(iter))
  for name in ["invalid.bin","long.txt"] do
    let rejected=must(f.open(name))
    match f.read_line(rejected) Error(_) -> nil Ok(_) -> assert(false) end
    assert(f.tell(rejected)==0,"failed text read preserves the byte position")
    assert(must(f.read(rejected,1)).len()==1,"raw reads recover from a rejected line")
    must(f.close(rejected))
  end

  let unbuffered=must(f.open("unbuffered.txt","w"))
  must(f.set_buffer(unbuffered,"none",0))
  assert(f.get_buffer(unbuffered).size==0)
  must(f.write(unbuffered,"now"))
  assert(must(fs.read_text("user:unbuffered.txt"))=="now")
  must(f.set_buffer(unbuffered,"full",0))
  must(f.write(unbuffered,"!"))
  assert(must(fs.read_text("user:unbuffered.txt"))=="now!")
  must(f.close(unbuffered))
  let writer=must(filesystem.open("buffered.txt","w"))
  must(f.set_buffer(writer,"full",8))
  must(f.write(writer,"abc"))
  assert(f.tell(writer)==3 and must(fs.read_text("user:buffered.txt"))=="")
  assert(must(f.get_size(writer))==3 and must(fs.read_text("user:buffered.txt"))=="")
  must(f.write(writer,"defghij"))
  assert(must(fs.read_text("user:buffered.txt"))=="abcdefgh")
  must(f.flush(writer))
  assert(must(fs.read_text("user:buffered.txt"))=="abcdefghij")
  must(f.seek(writer,2))
  must(f.write(writer,"Z"))
  must(f.close(writer))
  assert(must(fs.read_text("user:buffered.txt"))=="abZdefghij")

  let line=must(f.open("line.txt","w"))
  must(f.set_buffer(line,"line",32))
  must(f.write(line,"one\\ntwo"))
  assert(must(fs.read_text("user:line.txt"))=="one\\n")
  must(f.write(line," three\\nfour"))
  assert(must(fs.read_text("user:line.txt"))=="one\\ntwo three\\n")
  must(f.sync(line))
  assert(must(fs.read_text("user:line.txt"))=="one\\ntwo three\\nfour")
  must(f.close(line))
  let append=must(f.open("line.txt","a"))
  assert(f.tell(append)==18)
  must(f.write(append,"!"))
  fails(fn() f.seek(append,0) end)
  must(f.close(append))
  assert(must(fs.read_text("user:line.txt"))=="one\\ntwo three\\nfour!")

  let saved=nil
  must(f.with_file("scoped.txt","w",fn(file)
    saved=file
    must(f.set_buffer(file,"full"))
    must(f.write(file,"saved on error"))
    return nil
  end))
  assert(not f.is_open(saved) and must(fs.read_text("user:scoped.txt"))=="saved on error")
  match f.with_file("scoped.txt","w",fn(file)
    saved=file
    must(f.write(file,"fault path"))
    assert(false,"deliberate body failure")
  end)
    Error(_) -> nil
    Ok(_) -> assert(false)
  end
  assert(not f.is_open(saved))
  match f.with_file("close-error.txt","w",fn(file)
    saved=file
    must(f.set_buffer(file,"full"))
    must(f.write(file,"pending"))
    fs.close(file.handle)
  end)
    Error(_) -> nil
    Ok(_) -> assert(false,"failed flush must be reported")
  end
  assert(not f.is_open(saved),"scoped failed flush must release the handle")
  let exclusive=must(f.open("exclusive.txt","create"))
  must(f.write(exclusive,"original"))
  must(f.close(exclusive))
  match f.open("exclusive.txt","create") Error(_) -> nil Ok(_) -> assert(false) end
  let overwrite=must(f.open("exclusive.txt","update"))
  must(f.write(overwrite,"new"))
  must(f.close(overwrite))
  assert(must(fs.read_text("user:exclusive.txt"))=="newginal")
  let named=filesystem.new_file_data(bytes.from_string("payload"),"folder/name.bin")
  let cloned=filesystem.file_data.clone(named)
  cloned.filename="other.dat"
  assert(named.filename=="folder/name.bin" and named.data==cloned.data)
  assert(filesystem.file_data.get_extension(named)=="bin")
  must(filesystem.append("scoped.txt"," appended"))
  assert(must(fs.read_text("user:scoped.txt"))=="fault path appended")
  let named_read=must(filesystem.read_file_data("user:scoped.txt"))
  assert(filesystem.file_data.get_size(named_read)==19)
  match f.open("../outside","w") Error(_) -> nil Ok(_) -> assert(false) end
  match f.open("user:escape/keep.txt","w") Error(_) -> nil Ok(_) -> assert(false) end
  assert(not filesystem.exists("user:escape/keep.txt"))
  match f.open("res:input.bin","w") Error(_) -> nil Ok(_) -> assert(false) end
  match f.open("missing.bin") Error(_) -> nil Ok(_) -> assert(false) end
  fails(fn() f.open("bad","invalid") end)

  -- A large file built and read without holding its full contents in memory.
  let chunk=bytes.from_string(str.repeat("x",1048576))
  let large=must(f.open("large.bin","w"))
  for i in 0:34 do must(f.write(large,chunk)) end
  must(f.close(large))
  let large_read=must(f.open("user:large.bin"))
  assert(must(f.get_size(large_read))==34*1048576)
  must(f.seek(large_read,-4,"end"))
  assert(bytes.to_string(must(f.read(large_read,4)))==Ok("xxxx") and f.is_eof(large_read))
  must(f.close(large_read))
  let forgotten=must(f.open("forgotten.txt","w"))
  must(f.write(forgotten,"closed by partition teardown"))
  print("PASS: rooted streams, byte positions, CRLF/EOF, read/write buffers, seek, sync, append, scoped cleanup, capability refusal and files over 32 MiB")
end
fn verify() end
''')
    command=[host,'--headless','--verify','--frames','1','--save-dir',str(root/'saves'),str(root)]
    result=subprocess.run(command,capture_output=True,text=True,timeout=25)
    assert result.returncode==0,(result.stdout,result.stderr)
    print(result.stdout.strip())
    assert (root/'saves/large.bin').stat().st_size==34*1024*1024
    assert (root/'outside/keep.txt').read_text()=='outside grant'
    # Repeat resource reading from a deflated pack; no writes escape the granted root.
    with zipfile.ZipFile(root/'assets.zip','w',zipfile.ZIP_DEFLATED) as pack:
        pack.writestr('input.bin',(root/'res/input.bin').read_bytes())
    (root/'main.scua').write_text('''import filesystem
import bytes
let f=filesystem.stream
fn must(result) match result Ok(value) -> return value Error(reason) -> assert(false,tostring(reason)) end end
fn load()
 let file=must(f.open("input.bin"))
 assert(must(f.get_size(file))==19)
 must(f.seek(file,9))
 assert(must(f.read_line(file))=="world")
 must(f.seek(file,0))
 assert(bytes.to_string(must(f.read(file,5)))==Ok("hello"))
 must(f.close(file))
end
fn verify() end
''')
    result=subprocess.run([host,'--headless','--verify','--frames','1','--save-dir',str(root/'saves'),'--mount',str(root/'assets.zip'),str(root)],capture_output=True,text=True,timeout=10)
    assert result.returncode==0,(result.stdout,result.stderr)
    print('PASS: deflated resource streams seek forward/backward')
