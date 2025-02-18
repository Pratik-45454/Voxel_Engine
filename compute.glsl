#version 430 core

layout(local_size_x = 16, local_size_y = 16) in;
layout (rgba32f, binding = 0) uniform image2D resultImage;

struct FlattenedNode {
    bool IsLeaf;
    int childIndices[8];
    vec4 color;
};

layout(std430, binding = 0) buffer NodeBuffer {
    FlattenedNode nodes[];
};

uniform vec2 iResolution;
uniform mat4 viewMatrix;
uniform vec3 cameraPos;
uniform float fov;
uniform vec3 cubeCentres[100];
uniform mat4 rot;
uniform vec3 cube_size_half;
uniform int numCubes;
uniform vec3 minBound;
uniform vec3 maxBound;

const int MAX_STEPS = 100;
const float MAX_DIST = 50.0;
const float SURFACE_DIST = 1.5;

float cubeSDF(vec3 p, vec3 cubeCentre) {
    p -= cubeCentre;
    mat3 rot3 = mat3(rot);
    vec3 rp = rot3 * p;
    vec3 d = abs(rp) - cube_size_half;
    return length(max(d, 0.0)) + min(max(d.x, max(d.y, d.z)), 0.0);
}

bool intersectAABB(vec3 ro, vec3 rd, vec3 minBound, vec3 maxBound) {
    vec3 t1 = (minBound - ro) / rd;
    vec3 t2 = (maxBound - ro) / rd;
    vec3 tmin = min(t1, t2);
    vec3 tmax = max(t1, t2);
    
    float tEnter = max(max(tmin.x, tmin.y), tmin.z);
    float tExit = min(min(tmax.x, tmax.y), tmax.z);

    return tEnter <= tExit && tExit > 0.0;
}

float rayMarch(vec3 ro, vec3 rd) {
    float dO = 0.0;
    for (int i = 0; i < MAX_STEPS; i++) {
        vec3 p = ro + rd * dO;
        float closestDist = 1e10;

        for (int j = 0; j < numCubes; j++) {
            if (!intersectAABB(ro, rd, cubeCentres[j] - cube_size_half, cubeCentres[j] + cube_size_half)) continue;
            float dS = cubeSDF(p, cubeCentres[j]);
            closestDist = min(closestDist, dS);
        }
        
        if ((MAX_DIST - dO) < closestDist) return MAX_DIST;
        dO += closestDist;
        if (dO > MAX_DIST || closestDist < SURFACE_DIST) break;
    }
    return dO;
}

void main() {
    ivec2 pixelCoords = ivec2(gl_GlobalInvocationID.xy);
    if (pixelCoords.x >= int(iResolution.x) || pixelCoords.y >= int(iResolution.y)) return;

    vec2 uv = (vec2(pixelCoords) / iResolution) * 2.0 - 1.0;
    uv.x *= iResolution.x / iResolution.y;
    
    vec3 rayDirCameraSpace = normalize(vec3(uv, -1.0 / tan(radians(fov / 2.0))));
    mat3 invViewMatrix = mat3(transpose(viewMatrix));
    vec3 rayDirWorldSpace = normalize(invViewMatrix * rayDirCameraSpace);

    vec3 rayOrigin = cameraPos;

    if (!intersectAABB(rayOrigin, rayDirWorldSpace, minBound, maxBound)) {
        imageStore(resultImage, pixelCoords, vec4(0.0, 0.0, 0.0, 1.0)); // No hit
        return;
    }

    float d = rayMarch(rayOrigin, rayDirWorldSpace);
    vec4 color = d < MAX_DIST ? vec4(1.0, 0.0, 0.0, 1.0) : vec4(0.0, 0.0, 1.0, 1.0); // Red for hit, Blue for no hit

    imageStore(resultImage, pixelCoords, color); // Store result in texture
}
