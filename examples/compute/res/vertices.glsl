#pragma language glsl4
layout(local_size_x = 4, local_size_y = 1, local_size_z = 1) in;
struct VertexData { vec4 VertexPosition; vec4 VertexColor; };
layout(std430) buffer Vertices { VertexData vertices[]; };
layout(std430) readonly buffer Tint { vec4 values[]; } tints[2];
uniform float shift;
uniform vec4 tint;
void computemain()
{
    uint index = love_GlobalThreadID.x;
    if (index >= 4u) return;
    vec2 corners[4] = vec2[4](vec2(-50,-50),vec2(50,-50),vec2(-50,50),vec2(50,50));
    vertices[index].VertexPosition = vec4(corners[index] + vec2(shift,0),0,1);
    vertices[index].VertexColor = tint * (tints[0].values[0] + tints[1].values[0]);
}
