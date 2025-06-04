#version 330 core
layout (location = 0) in vec3 aPos;

uniform mat4 u_modelMatrix;
uniform mat4 u_viewMatrix;
uniform mat4 u_projectionMatrix;

out vec3 v_worldPos;

void main()
{
    v_worldPos = vec3(u_modelMatrix * vec4(aPos, 1.0));
    gl_Position = u_projectionMatrix * u_viewMatrix * u_modelMatrix * vec4(aPos, 1.0);
}