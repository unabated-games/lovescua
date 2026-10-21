#pragma language glsl3
#ifdef VERTEX
out vec4 VaryingColor;
out vec4 VaryingTexCoord;
void vertexmain()
{
    VaryingColor=vec4(1);
    VaryingTexCoord=vec4(0);
    vec2 corners[3]=vec2[3](vec2(0,0),vec2(64,0),vec2(0,64));
    love_Position=TransformProjectionMatrix * vec4(corners[love_VertexID],0,1);
}
#endif
#ifdef PIXEL
vec4 effect(vec4 color,Image tex,vec2 uv,vec2 pixel)
{
    return vec4(0.2,0.8,0.9,1);
}
#endif
