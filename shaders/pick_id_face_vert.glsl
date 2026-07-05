#version 430 core
// PICK-ID face pass (mate-selector P1): plain MVP transform; ids come out in the frag.
layout(location = 0) in vec3 aPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

void main()
{
    gl_Position = projection * view * model * vec4(aPos, 1.0);
}
