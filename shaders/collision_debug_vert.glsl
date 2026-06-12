#version 430 core

layout(location = 0) in vec3 aPos;

uniform mat4 u_mvp;

void main()
{
    gl_Position = u_mvp * vec4(aPos, 1.0);
    // Small depth bias toward the camera so wireframes win the depth fight
    // against the surfaces they were cooked from.
    gl_Position.z -= 0.0015 * gl_Position.w;
}
