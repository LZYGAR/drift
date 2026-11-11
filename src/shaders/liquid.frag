#version 440
// Liquid light for the cinema backdrop: a few large, soft pools of the cover's colours that drift and merge like
// ink in water. Nothing has an edge (every pool is a gaussian, and the field is warped), so no shape can be made out;
// what you see is colour moving. The pools swell with their part of the spectrum, the biggest with the bass, and
// the whole field brightens on each kick. Premultiplied light, drawn over the colour mesh.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 primary;
    vec4 secondary;
    vec4 ambient;
    vec4 bandsA;   // spectrum, eight bands low to high
    vec4 bandsB;
    vec2 size;
    float time;
    float level;
    float bass;
    float ripple;  // unused by this pass (kept so the uniform block matches LiquidField.qml)
    float kick;
} u;

void main() {
    vec2 uv = qt_TexCoord0;
    float aspect = u.size.x / max(u.size.y, 1.0);
    float t = u.time;
    vec2 p = vec2(uv.x * aspect, uv.y);
    // Warp the space a little, slowly, so the pools' outlines ripple like a liquid surface.
    p += 0.06 * vec2(sin(p.y * 3.1 + t * 0.7), cos(p.x * 2.7 - t * 0.6));

    float lows = (u.bandsA.x + u.bandsA.y) * 0.5;
    float lowMid = (u.bandsA.z + u.bandsA.w) * 0.5;
    float highMid = (u.bandsB.x + u.bandsB.y) * 0.5;
    float highs = (u.bandsB.z + u.bandsB.w) * 0.5;
    float swell[5] = float[5](lows + 0.6 * u.bass, lowMid, highMid, highs, 0.5 * (lows + highMid));
    vec3 tint[5] = vec3[5](u.primary.rgb, u.secondary.rgb, mix(u.primary.rgb, vec3(1.0), 0.3),
                           mix(u.secondary.rgb, vec3(1.0), 0.25), mix(u.primary.rgb, u.ambient.rgb, 0.5));

    vec3 light = vec3(0.0);
    float weight = 0.0;
    for (int i = 0; i < 5; ++i) {
        float fi = float(i);
        vec2 c = vec2(aspect * (0.5 + 0.36 * sin(t * (0.11 + 0.03 * fi) + fi * 1.7)),
                      0.5 + 0.32 * cos(t * (0.09 + 0.025 * fi) + fi * 2.3));
        float r = 0.22 + 0.06 * fi * 0.3 + 0.20 * swell[i] + 0.06 * u.kick;
        float d = length(p - c);
        float w = exp(-(d * d) / (r * r));
        light += tint[i] * w;
        weight += w;
    }
    // Brighter where pools overlap, and a lift on every kick; never blown out.
    vec3 rgb = light * (0.36 + 0.22 * u.level + 0.16 * u.kick);
    float a = clamp(weight * (0.30 + 0.15 * u.level), 0.0, 0.75);
    fragColor = vec4(rgb, a) * u.qt_Opacity;
}
