extern Image depth_texture;
vec4 effect(vec4 color,Image tex,vec2 uv,vec2 pixel)
{
    float d=Texel(depth_texture,vec2(0.5)).r;
    return vec4(d,d,d,1);
}
