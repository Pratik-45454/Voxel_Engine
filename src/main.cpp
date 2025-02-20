#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <shader/shader_m.h>
#include <shader/compute.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <camera.h>
#include <vector>
#include <cmath>

#include <cmath>

float generateTerrainNoise(float x, float z) {
    float height = 0.0f;
    float freq = 0.02f;  // Lower base frequency for larger mountains
    float amp = 50.0f;   // Increased amplitude for taller peaks
    float persistence = 0.2f; // Controls how quickly amplitudes decrease
    int octaves = 9; // More octaves for extra detail

    for (int i = 0; i < octaves; ++i) {
        height += std::sin(x * freq) * std::cos(z * freq) * amp;
        freq *= 2.0f;  // Increase frequency each octave
        amp *= persistence;  // Decrease amplitude each octave
    }
    
    // Exaggerate mountains with power-based scaling
    height = std::pow(height * 0.03f, 3.0f);  // Cubic transformation for sharper peaks

    return height * 10.0f;  // Scale final height appropriately
}


void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void processInput(GLFWwindow *window);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scrool_callback(GLFWwindow* window, double xoffset, double yoffset);

const unsigned int SCR_WIDTH = 800;
const unsigned int SCR_HEIGHT = 600;

// Set camera to view a large terrain.
glm::vec3 cameraPos   = glm::vec3(50.0f, 30.0f, 120.0f);
glm::vec3 cameraFront = glm::vec3(0.0f, -0.3f, -1.0f);
glm::vec3 cameraUp    = glm::vec3(0.0f, 1.0f,  0.0f);

bool firstMouse = true;
float yaw   = -90.0f;
float pitch = -20.0f;
float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
float fov   = 45.0f;

float deltaTime = 0.0f;
float lastFrame = 0.0f;

struct FlattenedNode {
    bool IsLeaf = false;
    char padding1[3];       // Pad bool to 4 bytes
    int childIndices[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    char padding2[12];      // Pad to align vec4 to 16 bytes after the array
    glm::vec4 color = glm::vec4(1.0f); // default white
};

std::vector<FlattenedNode> m_nodes;

class SparseVoxelOctree {
public:
    SparseVoxelOctree(int size, int maxDepth);
    void Insert(glm::vec3 point, glm::vec4 color);
private:
    void InsertImpl(int nodeIndex, glm::ivec3 point, glm::vec4 color, glm::ivec3 position, int depth);
    int m_size;
    int m_maxDepth;
};

SparseVoxelOctree::SparseVoxelOctree(int size, int maxDepth)
    : m_size(size), m_maxDepth(maxDepth) {
    m_nodes.push_back(FlattenedNode()); // root node
}

void SparseVoxelOctree::Insert(glm::vec3 point, glm::vec4 color) {
    InsertImpl(0, glm::ivec3(point), color, glm::ivec3(0), 0);
}

void SparseVoxelOctree::InsertImpl(int nodeIndex, glm::ivec3 point, glm::vec4 color, glm::ivec3 position, int depth) {
    if (nodeIndex >= m_nodes.size()) {
        std::cout << "Index out of bounds" << std::endl;
        return;
    }
    FlattenedNode &node = m_nodes[nodeIndex];
    node.color = color;
    if (depth == m_maxDepth) {
        node.IsLeaf = true;
        return;
    }
    float size = m_size / std::exp2(depth);
    glm::ivec3 center = position + glm::ivec3(size / 2.0f);
    glm::ivec3 childPos = {
        (point.x >= center.x) ? 1 : 0,
        (point.y >= center.y) ? 1 : 0,
        (point.z >= center.z) ? 1 : 0
    };
    int childIndex = (childPos.x << 2) | (childPos.y << 1) | (childPos.z);
    if (node.childIndices[childIndex] == -1) {
        node.childIndices[childIndex] = m_nodes.size();
        m_nodes.push_back(FlattenedNode());
    }
    glm::ivec3 newPosition = position + childPos * glm::ivec3(size / 2);
    InsertImpl(node.childIndices[childIndex], point, color, newPosition, depth + 1);
}

int main() {
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Terrain Generation", NULL, NULL);
    if (window == nullptr) {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    Shader ourShader("coordinate_systems.glsl", "coordinate_systems1.glsl");
    ComputeShader computeShader("compute.glsl");

    float quadVertices[] = {
        -1.0f,  1.0f,
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f
    };

    int octreeSize = 1000;     // Corrected to match 0-100 world range
    int maxDepth = 9;
    SparseVoxelOctree octree(octreeSize, maxDepth);
    m_nodes.reserve(500000);
    
    int numSteps = 1 << maxDepth;
 int voxelSize = octreeSize / numSteps;

 float rockHeight = octreeSize * 0.01f; // Below 30% height → Rock
 float snowHeight = octreeSize * 0.013f; // Above 80% height → Snow

 for (int xi = 0; xi < numSteps; xi++) {
    int x = xi * voxelSize;
    for (int zi = 0; zi < numSteps; zi++) {
        int z = zi * voxelSize;
        float noiseHeight = generateTerrainNoise(static_cast<float>(x), static_cast<float>(z));

        int ySteps = std::max(1, static_cast<int>(std::ceil(noiseHeight / voxelSize)));

        for (int yi = 0; yi < ySteps; yi++) {
            int y = yi * voxelSize;

            // Define base colors
            glm::vec4 rockColor = glm::vec4(0.4f, 0.3f, 0.2f, 1.0f);  // Brownish rock
            glm::vec4 midColor  = glm::vec4(0.7f, 0.7f, 0.7f, 1.0f);  // Icy gray (transition)
            glm::vec4 snowColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);  // Pure white snow

            // Calculate blend factor for smooth gradient
            float t = glm::clamp((y - rockHeight) / (snowHeight - rockHeight), 0.0f, 1.0f);

            // Blend from rock → ice → snow
            glm::vec4 finalColor;
            if (t < 0.5f) {
                finalColor = glm::mix(rockColor, midColor, t * 2.0f); // Rock to ice
            } else {
                finalColor = glm::mix(midColor, snowColor, (t - 0.5f) * 2.0f); // Ice to snow
            }

            glm::vec3 pos(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z));
            octree.Insert(pos, finalColor);
        }
    }
}
   
    glm::vec3 minBound = glm::vec3(0, 0, 0);
    glm::vec3 maxBound = glm::vec3(octreeSize, octreeSize, octreeSize);

    unsigned int VBO, VAO;
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    GLuint texture;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA32F, SCR_WIDTH, SCR_HEIGHT);
    glBindImageTexture(0, texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);

    GLuint ssbo;
    glGenBuffers(1, &ssbo);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, ssbo);
    glBufferData(GL_SHADER_STORAGE_BUFFER, m_nodes.size() * sizeof(FlattenedNode), m_nodes.data(), GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    while (!glfwWindowShouldClose(window)) {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        processInput(window);

        computeShader.use();
        computeShader.setMat4("viewMatrix", glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp));
        computeShader.setVec3("cameraPos", cameraPos);
        computeShader.setFloat("fov", fov);
        computeShader.setVec2("iResolution", SCR_WIDTH, SCR_HEIGHT);
        computeShader.setVec3("minBound", minBound);
        computeShader.setVec3("maxBound", maxBound);

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo);
        computeShader.dispatch((SCR_WIDTH + 15) / 16, (SCR_HEIGHT + 15) / 16, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

        glfwSetCursorPosCallback(window, mouse_callback);
        glfwSetScrollCallback(window, scrool_callback);

        float fps = 1.0f / deltaTime;
        std::string title = "Terrain Generation | FPS: " + std::to_string(fps);
        glfwSetWindowTitle(window, title.c_str());

        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        ourShader.use();
        glBindTexture(GL_TEXTURE_2D, texture);
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glfwTerminate();
    return 0;
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height) {
    glViewport(0, 0, width, height);
}

void processInput(GLFWwindow *window) {
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);
    float cameraSpeed = 2.5f * deltaTime;
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        cameraPos += cameraSpeed * cameraFront;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
        cameraPos -= cameraSpeed * cameraFront;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
        cameraPos -= glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
        cameraPos += glm::normalize(glm::cross(cameraFront, cameraUp)) * cameraSpeed;
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
        cameraPos += cameraSpeed * cameraUp;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        cameraPos -= cameraSpeed * cameraUp;
}

void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    static bool firstMouse = true;
    static float lastX = SCR_WIDTH / 2.0f;
    static float lastY = SCR_HEIGHT / 2.0f;
    if (firstMouse) {
        lastX = static_cast<float>(xpos);
        lastY = static_cast<float>(ypos);
        firstMouse = false;
    }
    float xoffset = static_cast<float>(xpos) - lastX;
    float yoffset = lastY - static_cast<float>(ypos);
    lastX = static_cast<float>(xpos);
    lastY = static_cast<float>(ypos);
    float sensitivity = 0.1f;
    xoffset *= sensitivity;
    yoffset *= sensitivity;
    yaw   += xoffset;
    pitch += yoffset;
    if (pitch > 89.0f)
        pitch = 89.0f;
    if (pitch < -89.0f)
        pitch = -89.0f;
    glm::vec3 front;
    front.x = cos(glm::radians(yaw)) * cos(glm::radians(pitch));
    front.y = sin(glm::radians(pitch));
    front.z = sin(glm::radians(yaw)) * cos(glm::radians(pitch));
    cameraFront = glm::normalize(front);
}

void scrool_callback(GLFWwindow* window, double xoffset, double yoffset) {
    if (fov >= 1.0f && fov <= 45.0f)
        fov -= static_cast<float>(yoffset);
    if (fov <= 1.0f)
        fov = 1.0f;
    if (fov >= 45.0f)
        fov = 45.0f;
}
