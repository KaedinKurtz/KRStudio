#version 430 core
layout(location = 0) in vec4 a_position;   // world-space
layout(location = 1) in vec4 a_color;
layout(location = 2) in float a_size;

uniform mat4 u_view;
uniform mat4 u_projection;

out vec4 v_color;

void main()
{
    vec4 viewPos = u_view * a_position;
    gl_Position  = u_projection * viewPos;

    /*  Make point size roughly constant in screen-space:      *
     *     size is given in world units; scale by 1/-z in view */
    float dist = -viewPos.z;
    gl_PointSize = a_size / max(dist, 0.001);

    v_color = a_color;
}
