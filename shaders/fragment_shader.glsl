

// =================================================================
//                      fragment_shader.glsl
// =================================================================
#version 330 core
out vec4 FragColor;

// Data received from the vertex shader (already in world space)
in vec3 FragPos;
in vec3 Normal;

// Uniforms from the C++ application
uniform vec3 objectColor;
uniform vec3 lightColor;
uniform vec3 lightPos;
uniform vec3 viewPos; // Camera's position

void main()
{
    // Ambient lighting component
    float ambientStrength = 0.3;
    vec3 ambient = ambientStrength * lightColor;
  	
    // Diffuse lighting component
    vec3 norm = normalize(Normal);
    vec3 lightDir = normalize(lightPos - FragPos);
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;
    
    // Specular lighting component
    float specularStrength = 0.5;
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 reflectDir = reflect(-lightDir, norm);  
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32);
    vec3 specular = specularStrength * spec * lightColor;
        
    vec3 result = (ambient + diffuse + specular) * objectColor;
    FragColor = vec4(result, 1.0);
}
