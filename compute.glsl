// GLSL Compute Shader for Sparse Voxel Octree Ray Marching

#version 430 core

layout(local_size_x = 16, local_size_y = 16) in;
layout (rgba32f, binding = 0) uniform image2D resultImage;

struct FlattenedNode {
    bool IsLeaf;
    int childIndices[8];
    vec4 color;
};

layout(std430, binding = 1) buffer NodeBuffer {
    FlattenedNode nodes[];
};

uniform vec2 iResolution;
uniform mat4 viewMatrix;
uniform vec3 cameraPos;
uniform float fov;
uniform vec3 minBound;
uniform vec3 maxBound;

const int MAX_DEPTH = 7;
const float MAX_DIST = 50.0;
const float SURFACE_DIST = 1.5;

bool intersectAABB(vec3 ro, vec3 rd, vec3 minB, vec3 maxB) {
    vec3 t1 = (minB - ro) / rd;
    vec3 t2 = (maxB - ro) / rd;
    vec3 tmin = min(t1, t2);
    vec3 tmax = max(t1, t2);
    float tEnter = max(max(tmin.x, tmin.y), tmin.z);
    float tExit = min(min(tmax.x, tmax.y), tmax.z);
    return tEnter <= tExit && tExit > 0.0;
}

int getChildIndex(vec3 point, vec3 pos, float size) {
    ivec3 childPos = ivec3(
        point.x >= (size * pos.x) + (size / 2.0),
        point.y >= (size * pos.y) + (size / 2.0),
        point.z >= (size * pos.z) + (size / 2.0)
    );
    return (childPos.x << 2) | (childPos.y << 1) | childPos.z;
}

vec4 traverseOctree(vec3 ro, vec3 rd) {
    int currentIndex = 0;
    float size = maxBound.x - minBound.x;
    vec3 nodePos = vec3(0.0);
    int depth = 0;

    while (depth <= MAX_DEPTH) {
        FlattenedNode node = nodes[currentIndex];
        if (node.IsLeaf) {
            return node.color;
        }

        int childIndex = getChildIndex(ro + rd * 0.001, nodePos, size);
        currentIndex = node.childIndices[childIndex];
        if (currentIndex == -1) return vec4(0.0, 0.0, 0.0, 1.0);

        size /= 2.0;
        nodePos += vec3(childIndex & 4, childIndex & 2, childIndex & 1) * size;
        depth++;
    }
    return vec4(0.0, 0.0, 0.0, 1.0);
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
        imageStore(resultImage, pixelCoords, vec4(0.0, 0.0, 0.0, 1.0));
        return;
    }

    vec4 color = traverseOctree(rayOrigin, rayDirWorldSpace);
    imageStore(resultImage, pixelCoords, color);
}
