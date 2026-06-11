#include "Mesh.h"
#include <glad/glad.h>
#include <utility>

Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices)
    : m_indexCount(static_cast<GLsizei>(indices.size())) {

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
                 vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(indices.size() * sizeof(unsigned int)),
                 indices.data(), GL_STATIC_DRAW);

    // location 0: position, 1: normal, 2: uv
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
}

Mesh::~Mesh() {
    release();
}

void Mesh::release() {
    if (m_ebo) glDeleteBuffers(1, &m_ebo);
    if (m_vbo) glDeleteBuffers(1, &m_vbo);
    if (m_vao) glDeleteVertexArrays(1, &m_vao);
    m_vao = m_vbo = m_ebo = 0;
    m_indexCount = 0;
}

Mesh::Mesh(Mesh&& other) noexcept
    : material(other.material),
      m_vao(other.m_vao), m_vbo(other.m_vbo), m_ebo(other.m_ebo), m_indexCount(other.m_indexCount) {
    other.m_vao = other.m_vbo = other.m_ebo = 0;
    other.m_indexCount = 0;
}

Mesh& Mesh::operator=(Mesh&& other) noexcept {
    if (this != &other) {
        release();
        material = other.material;
        m_vao = other.m_vao; m_vbo = other.m_vbo; m_ebo = other.m_ebo;
        m_indexCount = other.m_indexCount;
        other.m_vao = other.m_vbo = other.m_ebo = 0;
        other.m_indexCount = 0;
    }
    return *this;
}

void Mesh::draw() const {
    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}
