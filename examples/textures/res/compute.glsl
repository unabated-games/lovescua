#pragma language glsl4
layout(local_size_x=8,local_size_y=8,local_size_z=1) in;
layout(rgba8) uniform writeonly image2D target;
uniform float phase;
void computemain()
{
    ivec2 p=ivec2(love_GlobalThreadID.xy);
    vec4 color=vec4(float(p.x)/63.0,float(p.y)/63.0,phase,1.0);
    imageStore(target,p,color);
}
