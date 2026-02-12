#version 450

layout(set = 0, binding = 0, std140) uniform World {
    vec3 SKY_DIRECTION;
    vec3 SKY_ENERGY;
    vec3 SUN_DIRECTION;
    vec3 SUN_ENERGY;
};

layout(set=2, binding=0) uniform sampler2D TEXTURE;

// push constant: plain `time`
layout(push_constant) uniform PushConstants {
    float time;
};

layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 texCoord;

layout(location=0) out vec4 outColor;

float saturate(float x) { return clamp(x, 0.0, 1.0); }

vec3 rotateAroundAxis(vec3 v, vec3 axis, float angle) {
    axis = normalize(axis);
    float s = sin(angle);
    float c = cos(angle);
    return v * c + cross(axis, v) * s + axis * dot(axis, v) * (1.0 - c);
}

// cosine palette: A + B cos(2?(Cx + D))
vec3 palette(float x, vec3 A, vec3 B, vec3 C, vec3 D) {
    return A + B * cos(6.28318530718 * (C * x + D));
}

void main() {
    vec3 n = normalize(normal);
    vec3 albedo = texture(TEXTURE, texCoord).rgb;

    vec3 up = normalize(SKY_DIRECTION);

    // Orbit sun (shader-side day/night)
    float sunSpeed = 0.15;
    vec3 sunDir = normalize(rotateAroundAxis(normalize(SUN_DIRECTION), up, time * sunSpeed));

    // Sun elevation signals
    float sunUp = dot(sunDir, up);                 // 1 noon, 0 horizon, <0 night
    float day = smoothstep(-0.05, 0.05, sunUp);    // 0..1

    float golden = (1.0 - smoothstep(0.15, 0.55, sunUp)) * smoothstep(0.00, 0.10, sunUp);
    float twilight = smoothstep(-0.20, 0.02, sunUp) * (1.0 - smoothstep(0.02, 0.25, sunUp));

    // Pick an x for the palette:
    // x = 0 at noon-ish, x = 1 near/after horizon. Drives “sunset ramp”.
    float x = saturate(1.0 - smoothstep(0.10, 0.70, sunUp));

    // Variant selector driven by time (no extra uniforms):
    // v in [0,1), cycles slowly; use it to blend between a few presets.
    float v = fract(time * 0.2); // ~50 seconds per full cycle

    // Three preset palettes (artist knobs):
    // 1) peach/orange
    vec3 A1 = vec3(0.90, 0.55, 0.25);
    vec3 B1 = vec3(0.35, 0.30, 0.25);
    vec3 C1 = vec3(1.00, 1.00, 1.00);
    vec3 D1 = vec3(0.00, 0.10, 0.20);

    // 2) magenta/purple
    vec3 A2 = vec3(0.75, 0.35, 0.55);
    vec3 B2 = vec3(0.40, 0.25, 0.35);
    vec3 C2 = vec3(1.00, 1.00, 1.00);
    vec3 D2 = vec3(0.15, 0.35, 0.55);

    // 3) stylized green/purple “chromatic evening”
    vec3 A3 = vec3(0.45, 0.55, 0.70);
    vec3 B3 = vec3(0.35, 0.40, 0.45);
    vec3 C3 = vec3(1.00, 1.00, 1.00);
    vec3 D3 = vec3(0.55, 0.05, 0.25);

    // Blend between presets over time (piecewise, smooth)
    float w12 = smoothstep(0.20, 0.40, v) - smoothstep(0.45, 0.65, v); // bump for palette 2
    float w23 = smoothstep(0.55, 0.75, v);                             // ramps to palette 3
    float w31 = 1.0 - smoothstep(0.80, 0.98, v);                       // returns toward palette 1

    // Normalize-ish weights:
    float w1 = saturate(1.0 - w12) * saturate(w31);
    float w2 = saturate(w12);
    float w3 = saturate(w23) * saturate(1.0 - w31);

    float ws = max(1e-5, w1 + w2 + w3);
    w1 /= ws; w2 /= ws; w3 /= ws;

    // Phase offset also shifts over time for more variety:
    vec3 phaseJitter = vec3(0.07, 0.11, 0.13) * time * 0.05;

    vec3 A = w1*A1 + w2*A2 + w3*A3;
    vec3 B = w1*B1 + w2*B2 + w3*B3;
    vec3 C = w1*C1 + w2*C2 + w3*C3;
    vec3 D = w1*D1 + w2*D2 + w3*D3 + phaseJitter;

    // Use palette to color the sun + horizon sky during golden/twilight:
    vec3 sunsetColor = palette(x, A, B, C, D);

    // Sky tint: mostly base sky, but inject palette strongly near horizon at twilight/golden
    float hemi = 0.5 * dot(n, up) + 0.5;
    float horizonBoost = pow(1.0 - hemi, 2.0); // stronger near horizon on surfaces facing sideways
    vec3 skyTint = mix(vec3(1.0), sunsetColor, (golden + twilight) * horizonBoost);

    // Sun tint: palette is strongest during golden hour, otherwise near-white
    vec3 sunTint = mix(vec3(1.0, 1.0, 0.98), sunsetColor, golden + 0.5 * twilight);

    // Lighting
    vec3 sky = SKY_ENERGY * skyTint * hemi;
    float ndl = max(0.0, dot(n, sunDir));
    vec3 sun = (SUN_ENERGY * sunTint) * ndl * day;

    vec3 e = sky + sun;
    outColor = vec4(albedo, 1.0);
}
