#version 430 core

layout(local_size_x = 16, local_size_y = 16) in;
layout(rgba32f, binding = 0) uniform image2D resultImage;

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

const float MAX_DIST = 1000.0;
#define MAX_STACK_SIZE 64

// Stack entry structure for iterative traversal.
struct StackEntry {
    int nodeIndex;
    vec3 nodeMin;
    vec3 nodeMax;
    float tEnter;
};

// AABB intersection function that computes tEnter and tExit.
bool intersectAABB(vec3 ro, vec3 rd, vec3 boxMin, vec3 boxMax, out float tEnter, out float tExit) {
    vec3 t1 = (boxMin - ro) / rd;
    vec3 t2 = (boxMax - ro) / rd;
    vec3 tmin = min(t1, t2);
    vec3 tmax = max(t1, t2);
    tEnter = max(max(tmin.x, tmin.y), tmin.z);
    tExit  = min(min(tmax.x, tmax.y), tmax.z);
    return (tEnter <= tExit && tExit > 0.0);
}



// Traverse the octree with backtracking.
vec4 traverseOctree(vec3 ro, vec3 rd) {
    float tEnterRoot, tExitRoot;
    if (!intersectAABB(ro, rd, minBound, maxBound, tEnterRoot, tExitRoot)) {
        return vec4(0.0);
    }
    
    // Define a fixed-size stack.
    StackEntry stack[MAX_STACK_SIZE];
    int stackSize = 0;
    
    // Push the root node.
    stack[stackSize++] = StackEntry(0, minBound, maxBound, tEnterRoot);
    
    float bestT = MAX_DIST;
    vec4 hitColor = vec4(0.0);

    
    
    // While there are nodes in the stack...
    while (stackSize > 0) {
        // Find the stack entry with the smallest tEnter (closest intersection).
        int bestIndex = 0;
        float currentBest = stack[0].tEnter;
        for (int i = 1; i < stackSize; i++) {
            if (stack[i].tEnter < currentBest) {
                currentBest = stack[i].tEnter;
                bestIndex = i;
            }
        }
        
        // Pop the best entry.
        StackEntry entry = stack[bestIndex];
        stack[bestIndex] = stack[stackSize - 1];
        stackSize--;
        
        // If this entry is further than our current best hit, skip it.
        if (entry.tEnter > bestT) {
            continue;
        }
        
        FlattenedNode node = nodes[entry.nodeIndex];
        
        // If we hit a leaf, record its color and update bestT.
        if (node.IsLeaf) {
            hitColor = node.color;
            bestT = entry.tEnter;
            // Optionally, break here if you only need the first hit.
            break;
        }
        
        // Not a leaf: subdivide the current node's bounds.
        vec3 nodeMin = entry.nodeMin;
        vec3 nodeMax = entry.nodeMax;
        vec3 center = (nodeMin + nodeMax) * 0.5;
        
        // For each potential child...
        for (int child = 0; child < 8; child++) {
            int childNodeIndex = node.childIndices[child];
            if (childNodeIndex == -1)
                continue;
            
            // Compute the child's AABB based on the child's bit pattern.
            vec3 childMin;
            vec3 childMax;
            
            int bx = (child >> 2) & 1;
            int by = (child >> 1) & 1;
            int bz = (child) & 1;
            
            childMin.x = (bx == 0) ? nodeMin.x : center.x;
            childMax.x = (bx == 0) ? center.x  : nodeMax.x;
            childMin.y = (by == 0) ? nodeMin.y : center.y;
            childMax.y = (by == 0) ? center.y  : nodeMax.y;
            childMin.z = (bz == 0) ? nodeMin.z : center.z;
            childMax.z = (bz == 0) ? center.z  : nodeMax.z;
            
            float tChildEnter, tChildExit;
            if (intersectAABB(ro, rd, childMin, childMax, tChildEnter, tChildExit)) {
                if (tChildEnter < bestT && stackSize < MAX_STACK_SIZE) {
                    stack[stackSize++] = StackEntry(childNodeIndex, childMin, childMax, tChildEnter);
                }
            }
        }
    }
    
    return hitColor;
}

void main() {
    ivec2 pixelCoords = ivec2(gl_GlobalInvocationID.xy);
    if (pixelCoords.x >= int(iResolution.x) || pixelCoords.y >= int(iResolution.y))
        return;
    
    vec2 uv = (vec2(pixelCoords) / iResolution) * 2.0 - 1.0;
    uv.x *= iResolution.x / iResolution.y;
    vec3 rayDirCameraSpace = normalize(vec3(uv, -1.0 / tan(radians(fov / 2.0))));
    mat3 invViewMatrix = mat3(transpose(viewMatrix));
    vec3 rayDirWorldSpace = normalize(invViewMatrix * rayDirCameraSpace);
    vec3 rayOrigin = cameraPos;
    
    vec4 color = traverseOctree(rayOrigin, rayDirWorldSpace);
    imageStore(resultImage, pixelCoords, color);
}