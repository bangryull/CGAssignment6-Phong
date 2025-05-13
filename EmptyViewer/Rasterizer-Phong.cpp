#define _USE_MATH_DEFINES
#include <cmath>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <iostream>
#include <vector>
#include <algorithm>
#include <GL/freeglut.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

using namespace std;
using namespace glm;

const int Width = 512;
const int Height = 512;

vector<float> framebuffer(Width* Height * 3, 0.0f);
vector<float> depthBuffer(Width* Height, 1e9f);
vector<vec3> vertices;
vector<ivec3> triangles;
vector<vec3> vertex_normals;

mat4 model, view, proj, mvp;

vec3 compute_lighting(const vec3& position, const vec3& normal) {
    vec3 ka = vec3(0, 1, 0);
    vec3 kd = vec3(0, 0.5, 0);
    vec3 ks = vec3(0.5);
    float shininess = 32.0f;
    float Ia = 0.2f;
    vec3 lightPos = vec3(-4, 4, -3);
    vec3 ambient = Ia * ka;
    vec3 L = normalize(lightPos - position);
    vec3 V = normalize(-position);
    vec3 H = normalize(L + V);
    float diff = std::max(dot(normal, L), 0.0f);
    float spec = pow(std::max(dot(normal, H), 0.0f), shininess);
    vec3 color = ambient + kd * diff + ks * spec;
    return clamp(pow(color, vec3(1.0f / 2.2f)), vec3(0.0f), vec3(1.0f));
}

void create_scene() {
    int width = 32, height = 16;
    vertices.clear();
    triangles.clear();

    for (int j = 1; j < height - 1; ++j) {
        for (int i = 0; i < width; ++i) {
            float theta = float(j) / (height - 1) * M_PI;
            float phi = float(i) / (width - 1) * 2 * M_PI;
            float x = sin(theta) * cos(phi);
            float y = cos(theta);
            float z = -sin(theta) * sin(phi);
            vertices.emplace_back(x, y, z);
        }
    }

    vertices.emplace_back(0.0f, 1.0f, 0.0f);
    vertices.emplace_back(0.0f, -1.0f, 0.0f);

    for (int j = 0; j < height - 3; ++j) {
        for (int i = 0; i < width - 1; ++i) {
            triangles.emplace_back(j * width + i, (j + 1) * width + i + 1, j * width + i + 1);
            triangles.emplace_back(j * width + i, (j + 1) * width + i, (j + 1) * width + i + 1);
        }
    }

    int north_idx = vertices.size() - 2;
    for (int i = 0; i < width - 1; ++i)
        triangles.emplace_back(north_idx, i, i + 1);

    int south_idx = vertices.size() - 1;
    int base = (height - 3) * width;
    for (int i = 0; i < width - 1; ++i)
        triangles.emplace_back(south_idx, base + i + 1, base + i);
}

bool inside_triangle(const vec2& p, const vec2& a, const vec2& b, const vec2& c, vec3& bary) {
    vec2 v0 = b - a, v1 = c - a, v2 = p - a;
    float d00 = dot(v0, v0), d01 = dot(v0, v1), d11 = dot(v1, v1);
    float d20 = dot(v2, v0), d21 = dot(v2, v1);
    float denom = d00 * d11 - d01 * d01;
    if (denom == 0) return false;
    bary.y = (d11 * d20 - d01 * d21) / denom;
    bary.z = (d00 * d21 - d01 * d20) / denom;
    bary.x = 1.0f - bary.y - bary.z;
    return bary.x >= 0 && bary.y >= 0 && bary.z >= 0;
}

void rasterize_triangle(const vec4& v0, const vec4& v1, const vec4& v2, const ivec3& tri) {
    vec2 p0 = vec2(v0) / v0.w;
    vec2 p1 = vec2(v1) / v1.w;
    vec2 p2 = vec2(v2) / v2.w;
    p0 = (p0 + 1.0f) * 0.5f * vec2(Width, Height);
    p1 = (p1 + 1.0f) * 0.5f * vec2(Width, Height);
    p2 = (p2 + 1.0f) * 0.5f * vec2(Width, Height);

    vec3 wp0 = vec3(view * model * vec4(vertices[tri.x], 1.0f));
    vec3 wp1 = vec3(view * model * vec4(vertices[tri.y], 1.0f));
    vec3 wp2 = vec3(view * model * vec4(vertices[tri.z], 1.0f));

    vec3 n0 = vertex_normals[tri.x];
    vec3 n1 = vertex_normals[tri.y];
    vec3 n2 = vertex_normals[tri.z];

    int minX = std::max(0, (int)floor(std::min(std::min(p0.x, p1.x), p2.x)));
    int maxX = std::min(Width - 1, (int)ceil(std::max(std::max(p0.x, p1.x), p2.x)));
    int minY = std::max(0, (int)floor(std::min(std::min(p0.y, p1.y), p2.y)));
    int maxY = std::min(Height - 1, (int)ceil(std::max(std::max(p0.y, p1.y), p2.y)));

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            vec3 bary;
            vec2 p(x + 0.5f, y + 0.5f);
            if (inside_triangle(p, p0, p1, p2, bary)) {
                float z = bary.x * v0.z / v0.w + bary.y * v1.z / v1.w + bary.z * v2.z / v2.w;
                int idx = y * Width + x;
                if (z < depthBuffer[idx]) {
                    depthBuffer[idx] = z;
                    vec3 interp_pos = bary.x * wp0 + bary.y * wp1 + bary.z * wp2;
                    vec3 interp_normal = normalize(bary.x * n0 + bary.y * n1 + bary.z * n2);
                    vec3 color = compute_lighting(interp_pos, interp_normal);
                    framebuffer[3 * idx + 0] = color.r;
                    framebuffer[3 * idx + 1] = color.g;
                    framebuffer[3 * idx + 2] = color.b;
                }
            }
        }
    }
}

void render_scene() {
    create_scene();
    vertex_normals = vector<vec3>(vertices.size(), vec3(0));
    for (const auto& tri : triangles) {
        vec3 v0 = vertices[tri.x];
        vec3 v1 = vertices[tri.y];
        vec3 v2 = vertices[tri.z];
        vec3 normal = normalize(cross(v1 - v0, v2 - v0));
        vertex_normals[tri.x] += normal;
        vertex_normals[tri.y] += normal;
        vertex_normals[tri.z] += normal;
    }
    for (auto& n : vertex_normals) n = normalize(n);

    model = translate(mat4(1), vec3(0, 0, -7)) * scale(mat4(1), vec3(2));
    view = lookAt(vec3(0), vec3(0, 0, -1), vec3(0, 1, 0));
    proj = frustum(-0.1f, 0.1f, -0.1f, 0.1f, 0.1f, 1000.0f);
    mvp = proj * view * model;

    for (auto tri : triangles) {
        vec4 v0 = mvp * vec4(vertices[tri.x], 1.0f);
        vec4 v1 = mvp * vec4(vertices[tri.y], 1.0f);
        vec4 v2 = mvp * vec4(vertices[tri.z], 1.0f);
        rasterize_triangle(v0, v1, v2, tri);
    }
}

void display() {
    glClear(GL_COLOR_BUFFER_BIT);
    glDrawPixels(Width, Height, GL_RGB, GL_FLOAT, framebuffer.data());
    glutSwapBuffers();
}

int main(int argc, char** argv) {
    glutInit(&argc, argv);
    glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGB);
    glutInitWindowSize(Width, Height);
    glutCreateWindow("Phong Shading");
    glutDisplayFunc(display);
    render_scene();
    glutMainLoop();
    return 0;
}
