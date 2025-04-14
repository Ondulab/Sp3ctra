#include "AudioVisualizer.h"
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>

AudioVisualizer::AudioVisualizer(QWidget *parent)
    : QOpenGLWidget(parent), m_dataSize(0), m_dataUpdated(false) {
  // Activer le suivi de la souris
  setMouseTracking(true);

  // Définir la politique de taille
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

AudioVisualizer::~AudioVisualizer() {
  // Assurez-vous que le contexte OpenGL est courant
  makeCurrent();

  // Libérer les ressources OpenGL
  m_vbo.destroy();
  m_vao.destroy();

  // Relâcher le contexte
  doneCurrent();
}

void AudioVisualizer::updateData(const unsigned char *r, const unsigned char *g,
                                 const unsigned char *b, int size) {
  // Copier les données
  m_r.assign(r, r + size);
  m_g.assign(g, g + size);
  m_b.assign(b, b + size);
  m_dataSize = size;
  m_dataUpdated = true;

  // Demander une mise à jour
  update();
}

void AudioVisualizer::updateDataWithScroll(const unsigned char *r,
                                           const unsigned char *g,
                                           const unsigned char *b, int size) {
  // Stocker les nouvelles données
  m_r.assign(r, r + size);
  m_g.assign(g, g + size);
  m_b.assign(b, b + size);
  m_dataSize = size;

  // Ajouter la nouvelle ligne au début de l'historique
  m_historyR.insert(m_historyR.begin(),
                    std::vector<unsigned char>(r, r + size));
  m_historyG.insert(m_historyG.begin(),
                    std::vector<unsigned char>(g, g + size));
  m_historyB.insert(m_historyB.begin(),
                    std::vector<unsigned char>(b, b + size));

  // Limiter la taille de l'historique
  if (m_historyR.size() > m_historySize) {
    m_historyR.resize(m_historySize);
    m_historyG.resize(m_historySize);
    m_historyB.resize(m_historySize);
  }

  m_dataUpdated = true;

  // Demander une mise à jour
  update();
}

void AudioVisualizer::initializeGL() {
  // Initialiser les fonctions OpenGL
  initializeOpenGLFunctions();

  // Configurer OpenGL avec un fond noir complètement opaque
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

  // Désactiver le blending pour éviter les effets de transparence non désirés
  // qui peuvent causer l'effet de trame visible
  glDisable(GL_BLEND);

  // Initialiser les structures pour l'historique
  m_historySize = 600; // Hauteur approximative de la fenêtre
  m_historyR.clear();
  m_historyG.clear();
  m_historyB.clear();

  // Configurer les shaders et les buffers
  setupShaders();
  setupBuffers();
}

void AudioVisualizer::resizeGL(int w, int h) {
  // Mettre à jour le viewport
  glViewport(0, 0, w, h);
}

void AudioVisualizer::paintGL() {
  // Forcer un fond noir complètement opaque
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // Activer le blending avec un mode qui favorise l'opacité
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE,
              GL_ZERO); // Le pixel source remplace complètement la destination

  // Dessiner un rectangle noir qui couvre toute la zone de rendu
  m_program.bind();
  m_program.setUniformValue("forceColor", QVector4D(0.0f, 0.0f, 0.0f, 1.0f));
  m_program.setUniformValue("useForceColor", true);

  // Vertices pour le rectangle de fond
  float backgroundVertices[] = {
      -1.0f, -1.0f, 0.0f, 0.0f, 0.0f, // Bas gauche
      1.0f,  -1.0f, 0.0f, 0.0f, 0.0f, // Bas droite
      -1.0f, 1.0f,  0.0f, 0.0f, 0.0f, // Haut gauche
      1.0f,  1.0f,  0.0f, 0.0f, 0.0f  // Haut droite
  };

  // Créer un VBO temporaire pour le fond
  QOpenGLBuffer bgVbo(QOpenGLBuffer::VertexBuffer);
  bgVbo.create();
  bgVbo.bind();
  bgVbo.allocate(backgroundVertices, sizeof(backgroundVertices));

  // Configurer les attributs pour le fond
  m_program.enableAttributeArray(0);
  m_program.setAttributeBuffer(0, GL_FLOAT, 0, 2, 5 * sizeof(float));
  m_program.enableAttributeArray(1);
  m_program.setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 3,
                               5 * sizeof(float));

  // Dessiner le rectangle de fond
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

  // Nettoyer le VBO temporaire
  bgVbo.release();
  bgVbo.destroy();

  // Désactiver le forceColor pour les données suivantes
  m_program.setUniformValue("useForceColor", false);

  // Activer le blending avec un mode qui permet aux données de s'afficher
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // Activer le test de profondeur pour éviter les problèmes de transparence
  glEnable(GL_DEPTH_TEST);

  // Vérifier si nous avons des données à afficher
  if (m_historyR.empty() && m_dataSize == 0) {
    return;
  }

  // Mettre à jour les données si nécessaire
  if (m_dataUpdated) {
    // Reconstruire les vertices avec les nouvelles données
    m_vertices.clear();

    // Si nous avons un historique, l'utiliser
    if (!m_historyR.empty()) {
      // Pour chaque ligne dans l'historique
      for (int y = 0; y < m_historyR.size(); ++y) {
        for (int x = 0; x < m_dataSize; ++x) {
          // Position X normalisée
          float xPos = 2.0f * x / (float)(m_dataSize - 1) - 1.0f;

          // Position Y normalisée (inversée pour que les nouvelles lignes
          // apparaissent en haut)
          float yPos = 1.0f - 2.0f * y / (float)(m_historySize - 1);

          // Ajouter le vertex avec sa couleur
          m_vertices.push_back(xPos);                      // x
          m_vertices.push_back(yPos);                      // y
          m_vertices.push_back(m_historyR[y][x] / 255.0f); // r
          m_vertices.push_back(m_historyG[y][x] / 255.0f); // g
          m_vertices.push_back(m_historyB[y][x] / 255.0f); // b
        }
      }
    } else {
      // Sinon, juste afficher la ligne courante
      for (int i = 0; i < m_dataSize; ++i) {
        float x = 2.0f * i / (float)(m_dataSize - 1) - 1.0f;
        m_vertices.push_back(x);               // x
        m_vertices.push_back(0.0f);            // y
        m_vertices.push_back(m_r[i] / 255.0f); // r
        m_vertices.push_back(m_g[i] / 255.0f); // g
        m_vertices.push_back(m_b[i] / 255.0f); // b
      }
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

  // Créer une surface défilante avec des triangles
  int pointCount = m_vertices.size() / 5; // 5 valeurs par point (x, y, r, g, b)

  if (!m_historyR.empty() && m_dataSize > 0) {
    // Activer la primitive QUAD pour créer une surface continue
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Mettre à jour le VBO avec une approche d'entrelacement pour une surface
    // continue Cela permet de connecter les points entre les lignes de
    // l'historique
    std::vector<float> interleavedVertices;

    for (int y = 0; y < m_historyR.size() - 1; ++y) {
      for (int x = 0; x < m_dataSize; ++x) {
        // Coordonnées du point sur la ligne courante
        float xPos = 2.0f * x / (float)(m_dataSize - 1) - 1.0f;
        float y1 = 1.0f - 2.0f * y / (float)(m_historySize - 1);

        // Coordonnées du point sur la ligne suivante
        float y2 = 1.0f - 2.0f * (y + 1) / (float)(m_historySize - 1);

        // Créer un triangle entre les points
        interleavedVertices.push_back(xPos); // x position ligne courante
        interleavedVertices.push_back(y1);   // y position ligne courante
        // Couleur du point courant
        interleavedVertices.push_back(m_historyR[y][x] / 255.0f);
        interleavedVertices.push_back(m_historyG[y][x] / 255.0f);
        interleavedVertices.push_back(m_historyB[y][x] / 255.0f);

        // Ajouter le point correspondant sur la ligne suivante
        interleavedVertices.push_back(xPos); // x position ligne suivante
        interleavedVertices.push_back(y2);   // y position ligne suivante
        // Couleur du point sur la ligne suivante
        interleavedVertices.push_back(m_historyR[y + 1][x] / 255.0f);
        interleavedVertices.push_back(m_historyG[y + 1][x] / 255.0f);
        interleavedVertices.push_back(m_historyB[y + 1][x] / 255.0f);
      }
    }

    // Créer un buffer temporaire pour la surface
    QOpenGLBuffer surfaceVbo(QOpenGLBuffer::VertexBuffer);
    surfaceVbo.create();
    surfaceVbo.bind();
    surfaceVbo.allocate(interleavedVertices.data(),
                        interleavedVertices.size() * sizeof(float));

    // Configurer les attributs
    m_program.enableAttributeArray(0);
    m_program.setAttributeBuffer(0, GL_FLOAT, 0, 2, 5 * sizeof(float));
    m_program.enableAttributeArray(1);
    m_program.setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 3,
                                 5 * sizeof(float));

    // Dessiner des triangles pour chaque paire de lignes adjacentes
    for (int y = 0; y < m_historyR.size() - 1; ++y) {
      glDrawArrays(GL_TRIANGLE_STRIP, y * m_dataSize * 2, m_dataSize * 2);
    }

    // Libérer le buffer temporaire
    surfaceVbo.release();
    surfaceVbo.destroy();
  } else {
    // Si pas d'historique, juste dessiner les points avec une taille plus
    // grande
    glPointSize(5.0); // Points beaucoup plus grands pour éliminer les espaces
    glDrawArrays(GL_POINTS, 0, pointCount);
  }

  // Désactiver le test de profondeur après le rendu
  glDisable(GL_DEPTH_TEST);

  m_vao.release();
  m_program.release();
}

void AudioVisualizer::setupShaders() {
  // Vertex shader modifié pour augmenter la taille des points
  const char *vertexShaderSource = R"(
        #version 330 core
        layout (location = 0) in vec2 position;
        layout (location = 1) in vec3 color;
        
        out vec3 fragColor;
        
        void main()
        {
            gl_Position = vec4(position, 0.0, 1.0);
            // Augmenter la taille des points pour réduire l'effet de trame
            gl_PointSize = 2.0;
            fragColor = color;
        }
    )";

  // Fragment shader modifié pour gérer le fond noir opaque
  const char *fragmentShaderSource = R"(
        #version 330 core
        in vec3 fragColor;
        out vec4 outColor;
        
        // Uniforms pour forcer une couleur spécifique (utilisé pour le fond)
        uniform vec4 forceColor;
        uniform bool useForceColor;
        
        void main()
        {
            if (useForceColor) {
                // Utiliser la couleur forcée (pour le fond)
                outColor = forceColor;
                return;
            }
            
            // Traitement normal des données
            // Ne pas filtrer les pixels sombres pour que l'image soit visible
            
            // Rendre les pixels avec une opacité complète et augmenter l'intensité des couleurs
            // pour mieux les faire ressortir sur fond noir
            vec3 brightColor = fragColor * 1.5; // Augmenter l'intensité
            outColor = vec4(brightColor, 1.0);
        }
    )";

  // Compiler les shaders
  m_program.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource);
  m_program.addShaderFromSourceCode(QOpenGLShader::Fragment,
                                    fragmentShaderSource);
  m_program.link();

  // Initialiser les uniformes par défaut
  m_program.bind();
  m_program.setUniformValue("forceColor", QVector4D(0.0f, 0.0f, 0.0f, 1.0f));
  m_program.setUniformValue("useForceColor", false);
  m_program.release();
}

void AudioVisualizer::setupBuffers() {
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
  m_program.setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 3,
                               5 * sizeof(float));

  m_program.release();
  m_vbo.release();
  m_vao.release();
}
