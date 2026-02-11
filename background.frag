#version 450

layout(location = 0) out vec4 outColor;
layout(location = 0) in vec2 position; // expected 0..1 screen UV

layout(push_constant) uniform Push {
    float time;
};

float saturate(float x) { return clamp(x, 0.0, 1.0); }
float hash21(vec2 p) {
    // deterministic pseudo-random in [0,1)
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 34.345);
    return fract(p.x * p.y);
}

// cosine palette: A + B cos(2?(Cx + D))
vec3 palette(float x, vec3 A, vec3 B, vec3 C, vec3 D) {
    return A + B * cos(6.28318530718 * (C * x + D));
}

void main() {
    vec2 uv = position;

    // ---- "sun" cycle (no extra uniforms) ----
    // sun elevation in [-1,1], repeats every period seconds:
    float period = 80.0;
    float phi = 6.28318530718 * (time / period);
    float sunUp = sin(phi); // -1 night, 0 horizon, +1 noon

    float day = smoothstep(-0.05, 0.05, sunUp);
    float golden = (1.0 - smoothstep(0.15, 0.55, sunUp)) * smoothstep(0.00, 0.10, sunUp);
    float twilight = smoothstep(-0.20, 0.02, sunUp) * (1.0 - smoothstep(0.02, 0.25, sunUp));

    // x drives palette ramp: 0 noon-ish, 1 near horizon/night
    float x = saturate(1.0 - smoothstep(0.10, 0.70, sunUp));

    // ---- choose a sunset palette that drifts slowly over time ----
    vec3 A = vec3(0.55, 0.45, 0.55);
    vec3 B = vec3(0.45, 0.35, 0.35);
    vec3 C = vec3(1.00, 1.00, 1.00);
    vec3 D = vec3(0.00, 0.10, 0.20) + vec3(0.07, 0.11, 0.13) * (time * 0.03);

    vec3 sunset = palette(x, A, B, C, D);

    // base sky: bluer at noon, darker at night, palette near horizon at twilight
    float horizon = pow(1.0 - saturate(uv.y), 1.7);
    vec3 noonSky = vec3(0.55, 0.70, 0.95);
    vec3 nightSky = vec3(0.03, 0.05, 0.10);

    vec3 skyBase = mix(nightSky, noonSky, day);
    vec3 sky = skyBase + sunset * (golden + twilight) * horizon;

    // ---- "barcode blocks" pattern like your reference image ----
    // parameters:
    vec2 cellCount = vec2(10.0, 5.0); // number of big tiles
    vec2 cell = uv * cellCount;
    vec2 id = floor(cell);
    vec2 f = fract(cell);

    // keep margins white:
    float margin = 0.10;
    float inside = step(margin, f.x) * step(margin, f.y) * step(f.x, 1.0 - margin) * step(f.y, 1.0 - margin);

    // within a tile, create a 3x3-ish block grid and punch out black rectangles:
    vec2 g = f;
    // shrink to inner area:
    g = (g - margin) / (1.0 - 2.0 * margin);

    // rows of bars:
    float rows = 6.0;
    float cols = 6.0;

    vec2 bf = fract(g * vec2(cols, rows));
    vec2 bid = floor(g * vec2(cols, rows));

    // bar thickness and gaps:
    float gapx = 0.20;
    float gapy = 0.35;

    // randomized per-tile + per-subcell to decide if a bar exists
    float r0 = hash21(id * 17.0 + bid * 13.0);
    float on = step(0.42, r0); // probability of filled bar

    // make bars horizontal-ish rectangles
    float bar = step(gapx, bf.x) * step(gapy, bf.y) * step(bf.x, 1.0 - gapx) * step(bf.y, 1.0 - gapy);
    float ink = inside * on * bar;

    // animate pattern subtly with time (shifts which subcells are "on")
    // by offsetting the random seed slowly:
    float r1 = hash21(id * 17.0 + bid * 13.0 + floor(time * 0.7));
    float on2 = step(0.45, r1);
    ink *= mix(on, on2, 0.35);

    // ink color: dark during day, slightly tinted at twilight
    vec3 inkCol = mix(vec3(0.02), mix(vec3(0.02), sunset, 0.35), golden + twilight);

    // paper color: mostly light, but gently tinted by sky (so it matches the sun)
    vec3 paper = vec3(0.95);
    paper = mix(paper, paper * (0.75 + 0.25 * sky), 0.55); // subtle sky tint

    vec3 col = mix(paper, inkCol, ink);

    // Add a soft vignette so background feels less flat
    vec2 d = uv - 0.5;
    float vign = 1.0 - 0.45 * dot(d, d);
    col *= vign;

    outColor = vec4(col, 1.0);
}
