extern VolumeImage volume;
vec4 effect(vec4 color,Image tex,vec2 uv,vec2 pixel)
{
    return Texel(volume,vec3(0.5,0.5,0.75));
}
