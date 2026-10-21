#pragma language glsl3
uniform samplerBuffer colors[2];
vec4 effect(vec4 color, Image tex, vec2 uv, vec2 pixel)
{
    return color * (texelFetch(colors[0],0) + texelFetch(colors[1],0)) * 0.5;
}
