#ifndef POM_GLSL
#define POM_GLSL

const float POM_MIN_LAYERS = 8.0;
const float POM_MAX_LAYERS = 32.0;

// heightScale should be small, e.g. 0.02 - 0.08
vec2 parallax_occlusion_uv(
    sampler2D heightTex,
    vec2 baseUV,
    vec3 viewDirTS,   // tangent-space view dir, pointing from surface to camera
    float heightScale)
{
    vec3 V = normalize(viewDirTS);

    // avoid division blowups at grazing/backfacing angles
    float ndotv = max(V.z, 0.05);

    float numLayers = mix(POM_MAX_LAYERS, POM_MIN_LAYERS, ndotv);
    float layerDepth = 1.0 / numLayers;

    vec2 deltaUV = (V.xy / ndotv) * heightScale / numLayers;

    vec2 uv = baseUV;
    float currentLayerDepth = 0.0;
    float currentMapDepth = texture(heightTex, uv).r;

    while (currentLayerDepth < currentMapDepth)
    {
        uv -= deltaUV;
        currentMapDepth = texture(heightTex, uv).r;
        currentLayerDepth += layerDepth;
    }

    // linear refinement between previous and current step
    vec2 prevUV = uv + deltaUV;

    float afterDepth  = currentMapDepth - currentLayerDepth;
    float beforeDepth = texture(heightTex, prevUV).r - (currentLayerDepth - layerDepth);

    float w = afterDepth / (afterDepth - beforeDepth);
    w = clamp(w, 0.0, 1.0);

    return mix(uv, prevUV, w);
}

mat3 make_tbn(vec3 N, vec3 T, vec3 B)
{
    vec3 n = normalize(N);
    vec3 t = normalize(T - n * dot(n, T));
    vec3 b = normalize(B - n * dot(n, B) - t * dot(t, B));
    return mat3(t, b, n);
}

vec3 decode_normal_ts(vec3 enc)
{
    return normalize(enc * 2.0 - 1.0);
}

#endif