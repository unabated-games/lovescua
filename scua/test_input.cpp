// Exercise the real LÖVE joystick bindings with SDL's virtual gamepad backend.
#include "binding_util.h"
#include "joystick/sdl/JoystickModule.h"
#include <SDL3/SDL.h>
#include <cstring>
#include <iostream>
#include <memory>

int main()
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
        scua_register_const_int(p, nullptr, "DEVICE_ID", id);
        const char *source = R"SCUA(
import love_joystick as joystick
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
assert(joystick.axis(found,0)>0.49 and joystick.axis(found,0)<0.51)
assert(joystick.is_down(found,0))
assert(joystick.hat(found,0)=="u")
assert(joystick.gamepad_axis(found,"leftx")>0.49)
assert(joystick.gamepad_down(found,"a"))
assert(not joystick.has_sensor(found,"accelerometer"))
assert(not joystick.sensor_enabled(found,"accelerometer"))
match pcall(fn() joystick.set_sensor_enabled(found,"accelerometer",true) end)
  Ok(_) -> assert(false)
  Error(_) -> assert(true)
end
fn disconnected() assert(not joystick.is_connected(found)) end
)SCUA";
        char error[4096]{};
        if (scua_load_script(p, source, std::strlen(source), 8 * 1024 * 1024, 100000, error, sizeof(error)) !=
            SCUA_OK)
            throw std::runtime_error(error);
        module.removeJoystick(module.getJoystickFromID(id));
        auto callback = scua_get_global(p, "disconnected");
        if (scua_pcall(p, callback, nullptr, 0, nullptr, error, sizeof(error)) != SCUA_OK)
            throw std::runtime_error(error);
        if (scua_partition_free_checked(p) != SCUA_OK)
            throw std::runtime_error("checked teardown failed");
        partition.p = nullptr;
        if (state.liveObjects)
            throw std::runtime_error("joystick handles leaked");
        std::cout << "PASS: virtual joystick axes, buttons, hats, gamepad mapping and disconnect\n";
        return 0;
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
