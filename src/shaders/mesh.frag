#version 440
// The living backdrop: three cover colours poured through domain-warped noise.
// Bass pushes the warp harder and flares the light; time flows on its own.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 primary;
    vec4 secondary;
    vec4 ambient;
    vec4 base;
    vec2 size;
    float time;
    float pump;
    float calm;
    float breath;
} u;

float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x),
               mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}
float fbm(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 3; ++i) { v += a * noise(p); p = p * 2.03 + 11.7; a *= 0.5; }
    return v;
}

void main() {
    vec2 uv = qt_TexCoord0;
    float aspect = u.size.x / max(u.size.y, 1.0);
    vec2 p = vec2(uv.x * aspect, uv.y) * 1.25;
    float t = u.time;
    float drive = 1.0 + 1.6 * u.pump;                 // bass makes the flow churn
    vec2 q = vec2(fbm(p + vec2(0.0, t * 0.9)), fbm(p + vec2(5.2, 1.3) - vec2(t * 0.7, 0.0)));
    vec2 r = vec2(fbm(p + 2.2 * drive * q + vec2(1.7, 9.2) + t * 0.35),
                  fbm(p + 2.2 * drive * q + vec2(8.3, 2.8) - t * 0.3));
    float f = fbm(p + 2.6 * r);

    vec3 col = u.base.rgb;
    col = mix(col, u.ambient.rgb, smoothstep(0.10, 0.60, f));
    // The two cover colours own most of the picture; the flow decides where each one sits.
    float a = smoothstep(0.10, 0.65, length(q) + 0.35 * f);
    float b = smoothstep(0.20, 0.75, r.x + 0.25 * (1.0 - uv.y));
    col = mix(col, u.primary.rgb * (1.15 + 0.25 * u.breath), a);
    col = mix(col, u.secondary.rgb * 1.1, b * 0.8);
    // Light blooming from where the flow is densest, harder on every kick.
    float glow = smoothstep(0.55, 1.0, f) * (0.18 + 0.45 * u.pump);
    col += mix(u.primary.rgb, vec3(1.0), 0.45) * glow;
    // Calmer behind lists, so text stays readable.
    col = mix(col, mix(u.base.rgb, col, 0.55), u.calm);
    // Dither: no banding in the slow gradients.
    col += (hash(gl_FragCoord.xy + fract(t)) - 0.5) / 255.0;
    fragColor = vec4(col, 1.0) * u.qt_Opacity;
}
