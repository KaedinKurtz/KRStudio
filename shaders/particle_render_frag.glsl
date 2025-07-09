#version 430 core
in  vec4 v_color;
out vec4 fragColor;

void main()
{
    /* soft circular sprite -------------------------------- */
    float d = length(gl_PointCoord - vec2(0.5));
    float alpha = smoothstep(0.5, 0.45, d);   // fade out at edges
    fragColor = vec4(v_color.rgb, v_color.a * alpha);
}
