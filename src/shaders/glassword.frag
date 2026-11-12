#version 440
// The word "drift" as liquid glass.
//
// `sharp` is the word (white strokes), `soft` the same word blurred. The blurred copy is a height field: thresholding
// a mix of the two gives edges that melt and merge like liquid while `liquid` is high and settle to the true letters
// as it falls to 0; its slope is the surface normal, which lights the glass (a soft body, a hard highlight, a lit
// top-left rim and a shaded bottom-right one). Drops fly in and merge with the strokes while the word forms.
// Everything is black and white. Output is premultiplied.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float time;      // seconds
    float liquid;    // 1 molten, 0 settled
    float progress;  // 0 to 1, how much of the word is drawn
    float glow;      // halo strength
    vec2 size;       // in pixels
    vec4 tint;       // the glass colour (white unless following the Omarchy theme)
} u;
layout(binding = 1) uniform sampler2D sharp;
layout(binding = 2) uniform sampler2D soft;

float hash(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1, 0)), f.x), mix(hash(i + vec2(0, 1)), hash(i + vec2(1, 1)), f.x), f.y);
}
float fbm(vec2 p) {
    return 0.6 * noise(p) + 0.3 * noise(p * 2.1 + 3.7) + 0.1 * noise(p * 4.3 + 9.1);
}

void main() {
    vec2 uv = qt_TexCoord0;
    float aspect = u.size.x / max(1.0, u.size.y);
    vec2 p = vec2(uv.x * aspect, uv.y);

    // The surface is never quite still: a slow swell always, a strong one while molten.
    float t = u.time;
    vec2 flow = vec2(fbm(p * 2.6 + vec2(t * 0.35, -t * 0.22)), fbm(p * 2.6 + vec2(-t * 0.27, t * 0.31) + 5.2)) - 0.5;
    float wob = 0.006 + 0.075 * u.liquid;
    vec2 duv = uv + flow * wob * vec2(1.0 / aspect, 1.0);

    float c = texture(sharp, duv).a;
    float s = texture(soft, duv).a;

    // Drops that fly in and merge while the word forms.
    float k = pow(1.0 - clamp(u.progress, 0.0, 1.0), 1.5);
    float drops = 0.0;
    for (int i = 0; i < 7; ++i) {
        float fi = float(i);
        vec2 target = vec2((0.14 + 0.72 * fi / 6.0) * aspect, 0.52 + 0.08 * sin(fi * 2.1));
        vec2 dir = vec2(sin(fi * 2.4 + 0.6), cos(fi * 1.7 + 0.3));
        vec2 pos = target + dir * k * 0.55 + 0.03 * vec2(sin(t * 1.7 + fi), cos(t * 1.3 + fi * 2.0)) * k;
        float r = 0.085 * 4.0 * k * (1.0 - k);
        float d = length(p - pos);
        drops += r * r / (d * d + 0.0004);
    }

    float goo = clamp(0.22 + 0.7 * u.liquid, 0.0, 1.0);
    float f = mix(c, s * 1.35, goo) + 0.5 * min(drops, 1.6);
    float aa = max(fwidth(f) * 0.9, 0.012);
    float inside = smoothstep(0.5 - aa, 0.5 + aa, f);

    // Slope of the height field, two pixels apart.
    vec2 px = 2.0 / u.size;
    float hx = texture(soft, duv + vec2(px.x, 0.0)).a - texture(soft, duv - vec2(px.x, 0.0)).a;
    float hy = texture(soft, duv + vec2(0.0, px.y)).a - texture(soft, duv - vec2(0.0, px.y)).a;
    vec2 g = vec2(hx, hy);
    vec3 n = normalize(vec3(-g * 7.0, 1.0));
    vec3 light = normalize(vec3(-0.45, -0.65, 0.62));
    float diffuse = clamp(dot(n, light), 0.0, 1.0);
    vec3 halfway = normalize(light + vec3(0.0, 0.0, 1.0));
    float spec = pow(clamp(dot(n, halfway), 0.0, 1.0), 60.0);

    // Inside the glass: slow bright veins, bent by the surface.
    float veins = 0.5 + 0.5 * sin((p.x * 3.2 + p.y * 2.0) * 3.0 + fbm(p * 3.5 + n.xy * 1.5 + t * 0.25) * 6.0 + t * 0.7);
    float body = mix(0.60, 0.96, veins) * (0.72 + 0.34 * diffuse);

    // The rim: lit where the surface faces up and left, shaded where it turns away.
    float band = (1.0 - smoothstep(0.5, 0.78, f)) * inside;
    vec2 gn = g / (length(g) + 1e-4);
    float facing = dot(gn, normalize(vec2(1.0, 1.0)));
    float rimLight = band * clamp(facing, 0.0, 1.0);
    float rimShade = band * clamp(-facing, 0.0, 1.0);

    // A sheen that crosses the word now and then.
    float sweepAt = fract(t * 0.11) * 1.9 - 0.45;
    float sweep = exp(-pow((uv.x + uv.y * 0.25 - sweepAt) / 0.06, 2.0)) * 0.45;

    float lum = clamp(body + spec * 0.9 + rimLight * 0.55 - rimShade * 0.38 + sweep, 0.0, 1.0);
    vec3 col = lum * u.tint.rgb;

    // A soft light around the glass.
    float halo = s * 0.42 * u.glow * (1.0 - inside);

    vec3 rgb = col * inside + u.tint.rgb * halo;
    float alpha = clamp(inside + halo * (1.0 - inside), 0.0, 1.0);
    fragColor = vec4(rgb, alpha) * u.qt_Opacity;
}
