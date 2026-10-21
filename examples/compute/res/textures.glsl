extern Image layers[2];
vec4 effect(vec4 color, Image tex, vec2 uv, vec2 pixel)
{
    return color * (Texel(layers[0],vec2(0.5)) + Texel(layers[1],vec2(0.5))) * 0.5;
}
