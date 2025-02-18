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
#include <fastnoise/fastnoise.h>
float generateNoise(int x, int z, float scale) {
    // Generate a simple noise function using sine and cosine for both axes
    float noiseX = std::sin(x * scale);
    float noiseZ = std::cos(z * scale);
    
    // Combine the two to create 2D noise
    return (noiseX + noiseZ) * 0.5f; // Normalize the result between -1 and 1
}

void framebuffer_size_callback(GLFWwindow* window, int width, int height);
void processInput(GLFWwindow *window);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void scrool_callback(GLFWwindow* window, double xoffset, double yoffset);

const unsigned int SCR_WIDTH = 800;
const unsigned int SCR_HEIGHT = 600;

glm::vec3 cameraPos   = glm::vec3(0.0f, 0.0f,  5.0f);
glm::vec3 cameraFront = glm::vec3(0.0f, 0.0f, -1.0f);
glm::vec3 cameraUp    = glm::vec3(0.0f, 1.0f,  0.0f);
glm::vec3 newPos = cameraPos;
bool collision = false;

bool firstMouse = true;
float yaw = -90.0f;
float pitch = 0.0f;
float lastX = SCR_WIDTH / 2.0f;
float lastY = SCR_HEIGHT / 2.0f;
float fov = 45.0f;

float deltaTime = 0.0f;
float lastFrame = 0.0f;

// Sparse Voxel Octree

struct FlattenedNode {
    bool IsLeaf = false;
    int childIndices[8] = {-1};  // Indices of children in the flattened array
    glm::vec4 color = glm::vec4( 1.0f , 0.0f , 0.0f , 1.0f); // Color of the node
};
std::vector<FlattenedNode> m_nodes;

class SparseVoxelOctree {
public:
    SparseVoxelOctree(int size, int maxDepth);
    
    void Insert(glm::vec3 point, glm::vec4 color);
    
private:
    void InsertImpl(int nodeIndex, glm::vec3 point, glm::vec4 color, glm::vec3 position, int depth);
    int m_size;
    int m_maxDepth;
    
};

SparseVoxelOctree::SparseVoxelOctree(int size, int maxDepth) : m_size(size), m_maxDepth(maxDepth) {}

void SparseVoxelOctree::Insert(glm::vec3 point, glm::vec4 color) {
    InsertImpl(0, point, color, glm::vec3(0), 0);
}

void SparseVoxelOctree::InsertImpl(int nodeIndex, glm::vec3 point, glm::vec4 color, glm::vec3 position, int depth) {
    // Ensure that the node exists
    if (nodeIndex >= m_nodes.size()) {
        m_nodes.push_back(FlattenedNode());
    }

    FlattenedNode &node = m_nodes[nodeIndex];

    node.color = color;
    if (depth == m_maxDepth) {
        node.IsLeaf = true;
        return;
    }

    float size = m_size / std::exp2(depth);

    glm::vec3 childPos = {
        point.x >= (size * position.x) + (size / 2.f),
        point.y >= (size * position.y) + (size / 2.f),
        point.z >= (size * position.z) + (size / 2.f)
    };

    int childIndex = ((int)childPos.x << 0) | ((int)childPos.y << 1) | ((int)childPos.z << 2);

    // Update child index in the node
    if (node.childIndices[childIndex] == -1) {  // If child hasn't been created yet
        node.childIndices[childIndex] = m_nodes.size();  // Add child to the flattened array
        m_nodes.push_back(FlattenedNode());  // Create child node
    }

    position = {
        ((int)position.x << 1) | (int)childPos.x,
        ((int)position.y << 1) | (int)childPos.y,
        ((int)position.z << 1) | (int)childPos.z
    };

    InsertImpl(node.childIndices[childIndex], point, color, position, depth + 1);
}

int main()
{
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Ray Marching Cube", NULL, NULL);
    if (window == NULL)
    {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }

    glfwMakeContextCurrent(window);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
    {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    Shader ourShader("coordinate_systems.glsl", "coordinate_systems1.glsl");
    ComputeShader computeShader("compute.glsl");

    float quadVertices[] = {
        // positions
        -1.0f,  1.0f,
        -1.0f, -1.0f,
         1.0f, -1.0f,

        -1.0f,  1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f
    };

    // glm::vec3 translations[100];
    // int index = 0;
    // float offset = 2.5f;
    // for(int y = -5; y < 5; y += 1)
    // {
    //     for(int x = -5; x < 5; x += 1)
    //     {
    //         glm::vec2 translation;
    //         translation.x = (float)x  * offset;
    //         translation.y = (float)y  * offset;
    //         translations[index++] = glm::vec3(translation,0);
    //     }
    // }

    //generating the noise and fitting it into the octree
    SparseVoxelOctree octree(10, 7);

    for (int x = 0; x < 100; x++) {
        for (int z = 0; z < 100; z++) {
            int max_height = 10;
    
            // Scale factor for the sine-based noise
            float scale = 0.1f; // Adjust this for smoother or more "chaotic" noise
            float noise_value = generateNoise(x, z, scale);
            
            // Convert noise to a height value, ensure positive values for height
            int height = static_cast<int>(max_height / 2 * (noise_value + 1.0f)); // Normalize to [0, max_height]
            
            for (int y = 0; y < max_height + height; ++y) {
                octree.Insert(glm::vec3(x, y, z), glm::vec4(1.0f, 0.0f, 0.0f, 1.0f));
            }
        }
    }

    
    glm::vec3 minBound = glm::vec3(0, 0, 0);
    glm::vec3 maxBound = glm::vec3(10.0f, 10.0f, 10.0f);

    glm::vec3 size_half = glm::vec3(1.0f);

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


    

    // Main rendering loop
    while (!glfwWindowShouldClose(window))
    {
        float currentFrame = static_cast<float>(glfwGetTime());
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
        processInput(window);        
        computeShader.use();
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, ssbo);
        computeShader.dispatch((SCR_WIDTH ) / 16, (SCR_HEIGHT ) / 16, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        glfwSetCursorPosCallback(window, mouse_callback);
        glfwSetScrollCallback(window, scrool_callback);

        // FPS display in window title
        float fps = 1.0f / deltaTime;
        std::string title = "Ray Marching Cube | FPS: " + std::to_string(fps);
        glfwSetWindowTitle(window, title.c_str());

        glClearColor(0.2f, 0.3f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Setup view matrix for camera
        glm::mat4 view = glm::lookAt(cameraPos, cameraPos + cameraFront, cameraUp);

        // Use the compute shader first to set up the scene
        // for(unsigned int i = 0; i < 100; i++)
        // {
        //     computeShader.setVec3(("cubeCentres[" + std::to_string(i) + "]"), translations[i]);
        // }

        computeShader.setInt("numCubes", 100);

        // Set uniforms before dispatching compute shader
        computeShader.setMat4("viewMatrix", view);
        computeShader.setVec3("cameraPos", cameraPos);
        computeShader.setFloat("fov", fov);
        computeShader.setVec2("iResolution", SCR_WIDTH, SCR_HEIGHT);
        computeShader.setVec3("cameraFront", cameraFront);
        computeShader.setVec3("cube_size_half", size_half);
        computeShader.setVec3("minBound", minBound);
        computeShader.setVec3("maxBound", maxBound);
        glm::mat4 trans = glm::mat4(1.0f);
        trans = glm::rotate(trans , glm::radians(0.0f), glm::vec3(1.0f, 0.0f, 1.0f));
        computeShader.setMat4("rot", trans);

        // Dispatch compute shader
        for (int i = 0; i < m_nodes.size(); ++i) {
        std::cout << "Node " << i << ": IsLeaf = " << m_nodes[i].IsLeaf << std::endl;
        }

        std::cout<<"hello";
        // Bind the SSBO for use in the fragment shader
        // computeShader.setSSBO(0, ssbo);

        // Use the rendering shader for the final output
        ourShader.use();

        glBindTexture(GL_TEXTURE_2D, texture);

        // Draw the geometry (quad)
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

void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
}

void processInput(GLFWwindow *window)
{
    if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        glfwSetWindowShouldClose(window, true);

    float cameraSpeed = static_cast<float>(2.5 * deltaTime);
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

void mouse_callback(GLFWwindow* window, double xpos, double ypos)
{
    static bool firstMouse = true;
    static float lastX = SCR_WIDTH / 2.0f;
    static float lastY = SCR_HEIGHT / 2.0f;

    if (firstMouse)
    {
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

    yaw += xoffset;
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

void scrool_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    if (fov >= 1.0f && fov <= 45.0f)
        fov -= static_cast<float>(yoffset);
    if (fov <= 1.0f)
        fov = 1.0f;
    if (fov >= 45.0f)
        fov = 45.0f;
}
