extern CubeImage cube;
vec4 effect(vec4 color,Image tex,vec2 uv,vec2 pixel)
{
    return Texel(cube,vec3(1,0,0));
}
