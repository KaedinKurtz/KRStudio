#version 450 core

// Match the G-Buffer MRT layout
layout(location = 0) out vec4 gPosition;
layout(location = 1) out vec4 gNormal;
layout(location = 2) out vec4 gAlbedoAO;
layout (location = 3) out vec4 gMetalRough;
layout(location = 4) out vec4 gEmissive;

void main()
{
    // Write 0 to all attachments
    gPosition   = vec4(0.0);
    gNormal     = vec4(0.0);
    gAlbedoAO   = vec4(0.0);
    gMetalRough = vec4(0.0);
    gEmissive   = vec4(0.0);
}
