// Exercise the real LÖVE joystick bindings with SDL's virtual gamepad backend.
#include "binding_util.h"
#include "joystick/sdl/JoystickModule.h"
#include "sensor_events.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <iostream>
#include <memory>

int main(int argc, char **argv)
{
    try
    {
        if (!SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD))
            throw std::runtime_error(SDL_GetError());
        struct Quit
        {
            ~Quit()
            {
                SDL_QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMEPAD);
            }
        } quit;
        SDL_VirtualJoystickDesc desc{};
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
        desc.naxes = 6;
        desc.nbuttons = 16;
        desc.nhats = 1;
        desc.axis_mask = (1u << 6) - 1;
        desc.button_mask = (1u << 16) - 1;
        desc.name = "SCUA virtual gamepad";
        bool sensorsEnabled = false;
        SDL_VirtualJoystickSensorDesc sensorDescriptions[] = {{SDL_SENSOR_ACCEL, 60.0f},
                                                              {SDL_SENSOR_GYRO, 60.0f}};
        desc.nsensors = 2;
        desc.sensors = sensorDescriptions;
        desc.userdata = &sensorsEnabled;
        desc.SetSensorsEnabled = [](void *context, bool enabled)
        {
            *static_cast<bool *>(context) = enabled;
            return true;
        };
        auto id = SDL_AttachVirtualJoystick(&desc);
        if (!id)
            throw std::runtime_error(SDL_GetError());
        struct Detach
        {
            SDL_JoystickID id;
            ~Detach()
            {
                SDL_DetachVirtualJoystick(id);
            }
        } detach{id};
        auto device = SDL_OpenJoystick(id);
        if (!device)
            throw std::runtime_error(SDL_GetError());
        struct Close
        {
            SDL_Joystick *p;
            ~Close()
            {
                SDL_CloseJoystick(p);
            }
        } close{device};
        love::joystick::sdl::JoystickModule module;
        if (!SDL_SetJoystickVirtualAxis(device, 0, 16384) || !SDL_SetJoystickVirtualButton(device, 0, true) ||
            !SDL_SetJoystickVirtualHat(device, 0, SDL_HAT_UP))
            throw std::runtime_error(SDL_GetError());
        SDL_UpdateJoysticks();
        SDL_Event event{};
        event.type = SDL_EVENT_GAMEPAD_SENSOR_UPDATE;
        event.gsensor.which = id;
        event.gsensor.sensor = SDL_SENSOR_ACCEL;
        event.gsensor.data[0] = 1.25f;
        event.gsensor.sensor_timestamp = 1234567;
        auto sensor = lovescua::sensorEvent(event);
        if (!sensor.kind || std::strcmp(sensor.kind, "accelerometer") || sensor.id != id ||
            sensor.data[0] != 1.25f || sensor.timestamp != 1234567 || !sensor.gamepad)
            throw std::runtime_error("sensor event copy failed");
        event.gsensor.data[1] = std::numeric_limits<float>::quiet_NaN();
        if (lovescua::sensorEvent(event).kind)
            throw std::runtime_error("non-finite sensor event accepted");
        event = {};
        event.type = SDL_EVENT_SENSOR_UPDATE;
        event.sensor.which = 7;
        event.sensor.data[2] = 9.81f;
        if (!lovescua::sensorEvent(event, SDL_SENSOR_ACCEL).kind || lovescua::sensorEvent(event).kind)
            throw std::runtime_error("standalone sensor kind routing failed");
        lovescua::Host state;
        state.joysticks = &module;
        auto p = scua_partition_new();
        if (!p)
            throw std::runtime_error("partition allocation failed");
        struct Partition
        {
            scua_partition *p;
            ~Partition()
            {
                if (p)
                    scua_partition_free(p);
            }
        } partition{p};
        scua_UserdataLimits limits{};
        limits.struct_size = sizeof(limits);
        limits.version = 1;
        limits.max_bytes = 4096;
        limits.max_objects = 32;
        limits.max_types = 4;
        limits.max_leases = 16;
        limits.max_graph_work = 1024;
        limits.max_dependencies = 32;
        if (scua_userdata_configure(p, &limits) != SCUA_OK)
            throw std::runtime_error("userdata configuration failed");
        lovescua::registerInput(p, state);
        lovescua::registerResources(p, state);
        if (argc != 2 || scua_add_module_path(p, argv[1]) != SCUA_OK)
            throw std::runtime_error("expected platform example module directory");
        scua_register_const_int(p, nullptr, "DEVICE_ID", id);
        const char *source = R"SCUA(
import love_joystick as joystick
import love_object as object
import motion
let found=nil
for i in 0:joystick.count() do
  let j=joystick.get(i)
  if joystick.info(j).instance_id==DEVICE_ID then found=j end
end
assert(found!=nil)
assert(joystick.is_connected(found))
assert(joystick.info(found).gamepad)
assert(joystick.info(found).axes==6)
assert(joystick.info(found).buttons==16)
assert(joystick.info(found).connected_index>=0)
assert(joystick.info(found).axis_values.len()==6)
assert(joystick.info(found).joystick_type!="")
assert(joystick.mapping(found,"axis","leftx").index>=0)
assert(joystick.axis(found,0)>0.49 and joystick.axis(found,0)<0.51)
assert(joystick.is_down(found,0))
assert(joystick.hat(found,0)=="u")
assert(joystick.gamepad_axis(found,"leftx")>0.49)
assert(joystick.gamepad_down(found,"a"))
assert(joystick.has_sensor(found,"accelerometer"))
assert(joystick.has_sensor(found,"gyroscope"))
assert(not joystick.sensor_enabled(found,"accelerometer"))
joystick.set_sensor_enabled(found,"accelerometer",true)
joystick.set_sensor_enabled(found,"gyroscope",true)
assert(joystick.sensor_enabled(found,"accelerometer"))
assert(joystick.sensor_enabled(found,"gyroscope"))
assert(joystick.sensor_data(found,"accelerometer")==vec3(0,0,0))
assert(joystick.sensor_data(found,"gyroscope")==vec3(0,0,0))
joystick.set_sensor_enabled(found,"accelerometer",false)
joystick.set_sensor_enabled(found,"gyroscope",false)
let monitor=motion.new_monitor({sensor={has=fn(kind) return false end},joystick=joystick,close=object.close})
motion.refresh(monitor)
assert(joystick.sensor_enabled(found,"accelerometer"))
assert(joystick.sensor_enabled(found,"gyroscope"))
let matched=0
for reading in monitor.readings do
  if reading.id==DEVICE_ID then
    assert(reading.enabled)
    matched=matched+1
  end
end
assert(matched==2,"monitor must enable both native gamepad sensors")
motion.receive(monitor,motion.Source.Controller,DEVICE_ID,"gyroscope",vec3(1,2,3))
let received=false
for reading in monitor.readings do
  if reading.id==DEVICE_ID and reading.kind=="gyroscope" then
    assert(reading.received and reading.value==vec3(1,2,3))
    received=true
  end
end
assert(received)
fn disconnected() assert(not joystick.is_connected(found)) end
)SCUA";
        char error[4096]{};
        if (scua_load_script(p, source, std::strlen(source), 8 * 1024 * 1024, 100000, error, sizeof(error)) !=
            SCUA_OK)
            throw std::runtime_error(error);
        if (!sensorsEnabled)
            throw std::runtime_error("native gamepad sensors were not enabled");
        module.removeJoystick(module.getJoystickFromID(id));
        auto callback = scua_get_global(p, "disconnected");
        if (scua_pcall(p, callback, nullptr, 0, nullptr, error, sizeof(error)) != SCUA_OK)
            throw std::runtime_error(error);
        if (scua_partition_free_checked(p) != SCUA_OK)
            throw std::runtime_error("checked teardown failed");
        partition.p = nullptr;
        if (state.liveObjects)
            throw std::runtime_error("joystick handles leaked");
        std::cout << "PASS: virtual joystick axes, buttons, hats, gamepad mapping, sensor enabling and "
                     "disconnect\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
