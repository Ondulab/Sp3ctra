#include "AudioVisualizer.h"
#include <QOpenGLShaderProgram>
#include <QOpenGLContext>

AudioVisualizer::AudioVisualizer(QWidget *parent)
    : QOpenGLWidget(parent), m_dataSize(0), m_dataUpdated(false)
{
    // Activer le suivi de la souris
    setMouseTracking(true);
    
    // Définir la politique de taille
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

AudioVisualizer::~AudioVisualizer()
{
    // Assurez-vous que le contexte OpenGL est courant
    makeCurrent();
    
    // Libérer les ressources OpenGL
    m_vbo.destroy();
    m_vao.destroy();
    
    // Relâcher le contexte
    doneCurrent();
}

void AudioVisualizer::updateData(const unsigned char* r, const unsigned char* g, const unsigned char* b, int size)
{
    // Copier les données
    m_r.assign(r, r + size);
    m_g.assign(g, g + size);
    m_b.assign(b, b + size);
    m_dataSize = size;
    m_dataUpdated = true;
    
    // Demander une mise à jour
    update();
}

void AudioVisualizer::initializeGL()
{
    // Initialiser les fonctions OpenGL
    initializeOpenGLFunctions();
    
    // Configurer OpenGL
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Configurer les shaders et les buffers
    setupShaders();
    setupBuffers();
}

void AudioVisualizer::resizeGL(int w, int h)
{
    // Mettre à jour le viewport
    glViewport(0, 0, w, h);
}

void AudioVisualizer::paintGL()
{
    // Effacer l'écran
    glClear(GL_COLOR_BUFFER_BIT);
    
    // Vérifier si nous avons des données à afficher
    if (m_dataSize == 0) {
        return;
    }
    
    // Mettre à jour les données si nécessaire
    if (m_dataUpdated) {
        // Reconstruire les vertices avec les nouvelles données
        m_vertices.clear();
        
        for (int i = 0; i < m_dataSize; ++i) {
            // Position X normalisée
            float x = 2.0f * i / (float)(m_dataSize - 1) - 1.0f;
            
            // Ajouter le vertex avec sa couleur
            m_vertices.push_back(x);                      // x
            m_vertices.push_back(0.0f);                   // y
            m_vertices.push_back(m_r[i] / 255.0f);        // r
            m_vertices.push_back(m_g[i] / 255.0f);        // g
            m_vertices.push_back(m_b[i] / 255.0f);        // b
        }
        
        // Mettre à jour le VBO
        m_vao.bind();
        m_vbo.bind();
        m_vbo.allocate(m_vertices.data(), m_vertices.size() * sizeof(float));
        m_vbo.release();
        m_vao.release();
        
        m_dataUpdated = false;
    }
    
    // Dessiner les données
    m_program.bind();
    m_vao.bind();
    
    glDrawArrays(GL_POINTS, 0, m_dataSize);
    
    m_vao.release();
    m_program.release();
}

void AudioVisualizer::setupShaders()
{
    // Vertex shader
    const char* vertexShaderSource = R"(
        #version 330 core
        layout (location = 0) in vec2 position;
        layout (location = 1) in vec3 color;
        
        out vec3 fragColor;
        
        void main()
        {
            gl_Position = vec4(position, 0.0, 1.0);
            gl_PointSize = 1.0;
            fragColor = color;
        }
    )";
    
    // Fragment shader
    const char* fragmentShaderSource = R"(
        #version 330 core
        in vec3 fragColor;
        out vec4 outColor;
        
        void main()
        {
            outColor = vec4(fragColor, 1.0);
        }
    )";
    
    // Compiler les shaders
    m_program.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource);
    m_program.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource);
    m_program.link();
}

void AudioVisualizer::setupBuffers()
{
    // Créer et configurer le VAO
    m_vao.create();
    m_vao.bind();
    
    // Créer et configurer le VBO
    m_vbo.create();
    m_vbo.bind();
    
    // Définir les attributs de vertex
    m_program.bind();
    
    // Position (x, y)
    m_program.enableAttributeArray(0);
    m_program.setAttributeBuffer(0, GL_FLOAT, 0, 2, 5 * sizeof(float));
    
    // Couleur (r, g, b)
    m_program.enableAttributeArray(1);
    m_program.setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 3, 5 * sizeof(float));
    
    m_program.release();
    m_vbo.release();
    m_vao.release();
}
