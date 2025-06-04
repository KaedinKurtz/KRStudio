#version 330 core
layout (location = 0) in vec3 aPos; // Local quad vertex (e.g., on XY plane from -size to +size)

// Uniforms
uniform mat4 u_gridModelMatrix; // Grid's own transformation matrix (orientation and position)
uniform mat4 u_viewMatrix;
uniform mat4 u_projectionMatrix;

// Output to fragment shader
out vec3 v_worldPos;        // World position of the fragment
out vec2 v_gridPlaneCoord;  // 2D coordinate on the grid's LOCAL plane (before world transform)

void main()
{
    // aPos is a vertex of our local grid quad (e.g., a large square on XY plane, Y=0)
    // v_gridPlaneCoord will be used by the fragment shader to draw lines
    // relative to the grid's own local coordinate system.
    v_gridPlaneCoord = aPos.xz; // If local quad is on XZ plane, use X and Z
                                // If local quad is on XY plane, use X and Y: v_gridPlaneCoord = aPos.xy;

    // Transform the local quad vertex to its world position
    v_worldPos = vec3(u_gridModelMatrix * vec4(aPos, 1.0));
    
    // Standard MVP for screen position
    gl_Position = u_projectionMatrix * u_viewMatrix * vec4(v_worldPos, 1.0);
}
