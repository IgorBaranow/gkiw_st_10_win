#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>
#include <map>

// ───────────────────────────── GLSL sources ─────────────────────────────
static const char* vSrc = R"(#version 330 core
layout(location=0) in vec3 inPos;
layout(location=1) in vec3 inNormal;      // reserved for lighting
layout(location=2) in vec2 inTexCoord;
uniform mat4 MVP;
out vec2 vTex;
void main() {
    gl_Position = MVP * vec4(inPos,1.0);
    vTex = inTexCoord;
}
)";

static const char* fSrc = R"(#version 330 core
in vec2 vTex;
uniform sampler2D tex;
uniform int useTexture;
uniform vec3 baseColor;
uniform float opacity;
out vec4 fragColor;
void main() {
    vec3 color = (useTexture==1) ? texture(tex,vTex).rgb : baseColor;
    fragColor = vec4(color, opacity);
}
)";

// compile & link helpers
GLuint compileShader(const char* src, GLenum type) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512]; glGetShaderInfoLog(s, 512, nullptr, log);
        fprintf(stderr, "Shader compile error: %s\n", log);
        exit(1);
    }
    return s;
}
GLuint linkProgram(GLuint vs, GLuint fs) {
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512]; glGetProgramInfoLog(p, 512, nullptr, log);
        fprintf(stderr, "Program link error: %s\n", log);
        exit(1);
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
}

// mesh structure
struct Mesh {
    GLuint vao;
    GLuint vboPos, vboUV;
    GLuint tex;
    bool hasTex;
    glm::vec3 kd;
    GLsizei verts;
    bool transparent;
    float opacity;
};

std::vector<Mesh> meshes;
std::map<std::string, GLuint> texCache;

// load texture
GLuint loadTex(const std::string& f) {
    if (texCache.count(f)) return texCache[f];
    int w, h, n;
    unsigned char* d = stbi_load(f.c_str(), &w, &h, &n, 0);
    if (!d) { fprintf(stderr, "Failed to load %s\n", f.c_str()); return 0; }
    GLenum fmt = (n == 3 ? GL_RGB : GL_RGBA);
    GLuint id; glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, fmt, GL_UNSIGNED_BYTE, d);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    stbi_image_free(d);
    return texCache[f] = id;
}

// load OBJ+MTL
void loadOBJ(const char* file, const char* base = "./") {
    tinyobj::attrib_t A;
    std::vector<tinyobj::shape_t> S;
    std::vector<tinyobj::material_t> M;
    std::string warn, err;
    if (!tinyobj::LoadObj(&A, &S, &M, &warn, &err, file, base)) {
        fprintf(stderr, "%s %s\n", warn.c_str(), err.c_str()); exit(1);
    }
    for (const auto& shape : S) {
        int mid = shape.mesh.material_ids.empty() ? -1 : shape.mesh.material_ids[0];
        std::string matName = mid >= 0 ? M[mid].name : std::string();
        // skip water if unwanted
        if (shape.name == "Water" || matName == "water") continue;
        Mesh mesh{};
        mesh.kd = glm::vec3(1.0f);
        mesh.hasTex = false;
        mesh.tex = 0;
        mesh.opacity = 1.0f;
        mesh.transparent = false;
        if (mid >= 0) {
            auto& mat = M[mid];
            mesh.kd = glm::vec3(mat.diffuse[0], mat.diffuse[1], mat.diffuse[2]);
            mesh.opacity = mat.dissolve;
            mesh.transparent = mesh.opacity < 0.999f;
            if (!mat.diffuse_texname.empty()) {
                mesh.tex = loadTex(mat.diffuse_texname);
                mesh.hasTex = true;
            }
        }
        std::vector<float>P, UV;
        size_t off = 0;
        for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); ++f) {
            int fv = shape.mesh.num_face_vertices[f];
            for (int v = 0; v < fv; ++v) {
                auto idx = shape.mesh.indices[off + v];
                P.push_back(A.vertices[3 * idx.vertex_index + 0]);
                P.push_back(A.vertices[3 * idx.vertex_index + 1]);
                P.push_back(A.vertices[3 * idx.vertex_index + 2]);
                if (idx.texcoord_index >= 0) {
                    float u = A.texcoords[2 * idx.texcoord_index + 0];
                    float v = 1.0f - A.texcoords[2 * idx.texcoord_index + 1];
                    UV.push_back(u); UV.push_back(v);
                }
                else { UV.push_back(0.0f); UV.push_back(0.0f); }
            }
            off += fv;
        }
        mesh.verts = (GLsizei)(P.size() / 3);
        glGenVertexArrays(1, &mesh.vao);
        glBindVertexArray(mesh.vao);
        glGenBuffers(1, &mesh.vboPos);
        glBindBuffer(GL_ARRAY_BUFFER, mesh.vboPos);
        glBufferData(GL_ARRAY_BUFFER, P.size() * sizeof(float), P.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, 0);
        glGenBuffers(1, &mesh.vboUV);
        glBindBuffer(GL_ARRAY_BUFFER, mesh.vboUV);
        glBufferData(GL_ARRAY_BUFFER, UV.size() * sizeof(float), UV.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 0, 0);
        glBindVertexArray(0);
        meshes.push_back(mesh);
    }
}

// globals + callbacks
float aspect = 1.f, ax = 0, ay = 0, sx = 0, sy = 0;
void keyCB(GLFWwindow*, int k, int, int a, int) {
    if (a == GLFW_PRESS) {
        if (k == GLFW_KEY_LEFT)  sy = -1.5f;
        if (k == GLFW_KEY_RIGHT) sy = 1.5f;
        if (k == GLFW_KEY_UP)    sx = -1.5f;
        if (k == GLFW_KEY_DOWN)  sx = 1.5f;
    }
    else if (a == GLFW_RELEASE) { if (k == GLFW_KEY_LEFT || k == GLFW_KEY_RIGHT) sy = 0; if (k == GLFW_KEY_UP || k == GLFW_KEY_DOWN) sx = 0; }
}
void resizeCB(GLFWwindow*, int w, int h) { aspect = h ? float(w) / h : 1.f; glViewport(0, 0, w, h); }

// draw
void drawMesh(const Mesh& m, GLint locUT, GLint locBC, GLint locOp) {
    glUniform1i(locUT, m.hasTex); glUniform3fv(locBC, 1, glm::value_ptr(m.kd)); glUniform1f(locOp, m.opacity);
    if (m.hasTex) { glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, m.tex); }
    glBindVertexArray(m.vao);
    glDrawArrays(GL_TRIANGLES, 0, m.verts);
}

int main() {
    glfwInit();
    GLFWwindow* w = glfwCreateWindow(900, 700, "OpenGL Aquarium", NULL, NULL);
    glfwMakeContextCurrent(w);
    glewInit();
    glfwSetKeyCallback(w, keyCB);
    glfwSetFramebufferSizeCallback(w, resizeCB);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // white background
    glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
    GLuint prog = linkProgram(compileShader(vSrc, GL_VERTEX_SHADER), compileShader(fSrc, GL_FRAGMENT_SHADER));
    glUseProgram(prog);
    glUniform1i(glGetUniformLocation(prog, "tex"), 0);
    GLint locMVP = glGetUniformLocation(prog, "MVP");
    GLint locUT = glGetUniformLocation(prog, "useTexture");
    GLint locBC = glGetUniformLocation(prog, "baseColor");
    GLint locOp = glGetUniformLocation(prog, "opacity");
    loadOBJ("12987_Saltwater_Aquarium_v1_l1.obj", "./");
    resizeCB(w, 900, 700);
    glm::vec3 center(0, 0, 10.52905f);
    double prev = glfwGetTime();
    while (!glfwWindowShouldClose(w)) {
        double cur = glfwGetTime(); float dt = float(cur - prev); prev = cur;
        ax += sx * dt; ay += sy * dt;
        // camera: front view looking at origin
        glm::vec3 eye(0.0f, 0.0f, 40.0f);
        glm::mat4 V = glm::lookAt(eye, glm::vec3(0.0f), glm::vec3(0, 1, 0));
        glm::mat4 P = glm::perspective(glm::radians(60.f), aspect, 0.1f, 200.f);
        glm::mat4 M = glm::translate(glm::mat4(1), -center) * glm::rotate(glm::mat4(1), ax, glm::vec3(1, 0, 0)) * glm::rotate(glm::mat4(1), ay, glm::vec3(0, 1, 0));
        glm::mat4 MVP = P * V * M;
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glUniformMatrix4fv(locMVP, 1, GL_FALSE, glm::value_ptr(MVP));
        for (const auto& m : meshes) { if (!m.transparent) drawMesh(m, locUT, locBC, locOp); }
        glDepthMask(GL_FALSE);
        for (const auto& m : meshes) { if (m.transparent) drawMesh(m, locUT, locBC, locOp); }
        glDepthMask(GL_TRUE);
        glfwSwapBuffers(w);
        glfwPollEvents();
    }
    glfwTerminate();
    return 0;
}
