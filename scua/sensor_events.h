// SDL sensor events are copied into owner-thread script callbacks.
#pragma once
#include <SDL3/SDL.h>
#include <cmath>
namespace lovescua
{
struct SensorEvent
{
    const char *kind = nullptr;
    SDL_JoystickID id = 0;
    float data[3]{};
    uint64_t timestamp = 0;
    bool gamepad = false;
};
inline const char *sensorName(SDL_SensorType type)
{
    switch (type)
    {
    case SDL_SENSOR_ACCEL:
        return "accelerometer";
    case SDL_SENSOR_GYRO:
        return "gyroscope";
    case SDL_SENSOR_ACCEL_L:
        return "accelerometer_left";
    case SDL_SENSOR_GYRO_L:
        return "gyroscope_left";
    case SDL_SENSOR_ACCEL_R:
        return "accelerometer_right";
    case SDL_SENSOR_GYRO_R:
        return "gyroscope_right";
    default:
        return nullptr;
    }
}
inline SensorEvent sensorEvent(const SDL_Event &event, SDL_SensorType standalone = SDL_SENSOR_UNKNOWN)
{
    SensorEvent r;
    if (event.type == SDL_EVENT_GAMEPAD_SENSOR_UPDATE)
    {
        r.gamepad = true;
        r.id = event.gsensor.which;
        r.kind = sensorName(SDL_SensorType(event.gsensor.sensor));
        r.timestamp = event.gsensor.sensor_timestamp;
        for (int i = 0; i < 3; ++i)
            r.data[i] = event.gsensor.data[i];
    }
    else if (event.type == SDL_EVENT_SENSOR_UPDATE)
    {
        r.id = event.sensor.which;
        r.kind = sensorName(standalone);
        r.timestamp = event.sensor.sensor_timestamp;
        for (int i = 0; i < 3; ++i)
            r.data[i] = event.sensor.data[i];
    }
    for (float v : r.data)
        if (!std::isfinite(v))
            r.kind = nullptr;
    return r;
}
} // namespace lovescua
