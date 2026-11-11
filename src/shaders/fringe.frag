#version 440
// Chromatic fringe for the album cover in the cinema view. The red, green and blue channels are sampled a hair
// apart, further out towards the edges (like a real lens) and a little more on every kick, so the cover
// seems to shimmer at its rim and stays sharp in the middle. `amount` is in pixels of the source at the edge.
layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    float amount;   // fringe at the very edge, as a fraction of the width (0.004 is subtle)
    float pump;     // 0 to 1, the beat
} u;
layout(binding = 1) uniform sampler2D source;

void main() {
    vec2 uv = qt_TexCoord0;
    vec2 centre = uv - 0.5;
    float edge = dot(centre, centre) * 4.0;              // 0 in the middle, 1 at the corners
    vec2 dir = normalize(centre + 1e-5);
    float shift = u.amount * (0.15 + edge) * (1.0 + 1.5 * u.pump);
    float r = texture(source, uv + dir * shift).r;
    vec4 mid = texture(source, uv);
    float b = texture(source, uv - dir * shift).b;
    // Alpha follows the middle sample so the rounded mask of the cover is kept.
    fragColor = vec4(r * mid.a, mid.g, b * mid.a, mid.a) * u.qt_Opacity;
}
