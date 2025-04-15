# Documentation Technique: Mécanisme de Défilement dans CISYNTH

## Introduction

Ce document décrit le mécanisme de défilement vertical utilisé dans la version originale de CISYNTH (basée sur SFML/Xcode) et explique comment implémenter une fonctionnalité similaire dans la version Qt. Le défilement permet d'afficher l'historique des données audio sous forme de spectrogramme ou visualisation en temps réel.

## Table des matières

1. [Architecture du mécanisme de défilement original](#1-architecture-du-mécanisme-de-défilement-original)
2. [Fonctionnement détaillé du défilement SFML](#2-fonctionnement-détaillé-du-défilement-sfml)
3. [Implémentation pour Qt/OpenGL](#3-implémentation-pour-qtopengl)
4. [Optimisations possibles](#4-optimisations-possibles)
5. [Annexes](#5-annexes)

## 1. Architecture du mécanisme de défilement original

### 1.1 Principe général

Dans la version originale SFML, le défilement est implémenté à l'aide de deux textures:
- Une texture d'arrière-plan (`background_texture`)
- Une texture de premier plan (`foreground_texture`)

Ces textures sont utilisées en alternance dans un processus de "scrolling" vertical où:
1. Une nouvelle ligne de données est rendue en haut de l'écran
2. Le contenu existant est décalé d'un pixel vers le bas
3. Les lignes qui sortent de l'écran par le bas sont perdues

### 1.2 Structure du pipeline de données

```
┌────────────┐     ┌────────────┐     ┌────────────┐     ┌────────────┐
│ Acquisition│     │ Traitement │     │ Double     │     │ Rendu avec │
│ des données│─────│ audio      │─────│ Buffer     │─────│ défilement │
└────────────┘     └────────────┘     └────────────┘     └────────────┘
```

### 1.3 Composants clés

- **DoubleBuffer**: Structure qui contient les buffers RGB pour le traitement et l'affichage
- **printImageRGB()**: Fonction qui gère le rendu avec défilement dans la version SFML
- **updateVisualization()**: Méthode qui déclenche la mise à jour de l'affichage

## 2. Fonctionnement détaillé du défilement SFML

### 2.1 Structure de la fonction de défilement

La fonction `printImageRGB` dans `display.c` est le cœur du mécanisme de défilement:

```c
void printImageRGB(sfRenderWindow *window,
                  uint8_t *buffer_R,
                  uint8_t *buffer_G,
                  uint8_t *buffer_B,
                  sfTexture *background_texture,
                  sfTexture *foreground_texture)
{
    // 1. Création d'une image d'une ligne à partir des données RGB
    sfImage *image = sfImage_create(CIS_MAX_PIXELS_NB, 1);

    // 2. Remplissage de l'image avec les données RGB
    for (int x = 0; x < CIS_MAX_PIXELS_NB; x++)
    {
        sfColor color = sfColor_fromRGB(buffer_R[x], buffer_G[x], buffer_B[x]);
        sfImage_setPixel(image, x, 0, color);
    }

    // 3. Création d'une texture à partir de l'image
    sfTexture *line_texture = sfTexture_createFromImage(image, NULL);

    // 4. ÉTAPE CLÉ: Décalage du contenu d'un pixel vers le bas
    sfTexture_updateFromTexture(foreground_texture, background_texture, 0, 1);

    // 5. Ajout de la nouvelle ligne en haut
    sfTexture_updateFromImage(foreground_texture, image, 0, 0);

    // 6. Affichage de la texture mise à jour
    sfSprite *foreground_sprite = sfSprite_create();
    sfSprite_setTexture(foreground_sprite, foreground_texture, sfTrue);
    sfRenderWindow_drawSprite(window, foreground_sprite, NULL);
    sfRenderWindow_display(window);

    // 7. Sauvegarde pour la prochaine itération
    sfTexture_updateFromTexture(background_texture, foreground_texture, 0, 0);

    // 8. Nettoyage
    sfImage_destroy(image);
    sfTexture_destroy(line_texture);
    sfSprite_destroy(foreground_sprite);
}
```

### 2.2 Intégration dans la boucle principale

Dans la boucle principale (`main.c`), le défilement est déclenché à chaque fois que de nouvelles données sont disponibles:

```c
while (sfRenderWindow_isOpen(window))
{
    // Vérifier si le double buffer contient de nouvelles données
    pthread_mutex_lock(&db.mutex);
    int dataReady = db.dataReady;
    if (dataReady)
    {
        db.dataReady = 0;
    }
    pthread_mutex_unlock(&db.mutex);

    if (dataReady)
    {
        // Appliquer le défilement avec la nouvelle ligne de données
        printImageRGB(window,
                    db.processingBuffer_R,
                    db.processingBuffer_G,
                    db.processingBuffer_B,
                    backgroundTexture,
                    foregroundTexture);
        // ...
    }
    // ...
}
```

### 2.3 Diagramme séquentiel du mécanisme

```
┌───────────┐          ┌─────────────┐          ┌───────────────┐          ┌────────────┐
│Thread Audio│          │Double Buffer│          │Boucle Principale│          │Fonction de │
│            │          │             │          │                │          │Défilement  │
└─────┬─────┘          └──────┬──────┘          └───────┬────────┘          └─────┬──────┘
      │                       │                         │                          │
      │ Traite les données    │                         │                          │
      │───────────────────────>                         │                          │
      │                       │                         │                          │
      │                       │ Signale dataReady = 1   │                          │
      │                       │────────────────────────>│                          │
      │                       │                         │                          │
      │                       │                         │ Vérifie dataReady        │
      │                       │<────────────────────────│                          │
      │                       │                         │                          │
      │                       │ Confirme dataReady = 1  │                          │
      │                       │────────────────────────>│                          │
      │                       │                         │                          │
      │                       │                         │ Appelle printImageRGB    │
      │                       │                         │─────────────────────────>│
      │                       │                         │                          │
      │                       │                         │                          │ Crée image
      │                       │                         │                          │───────┐
      │                       │                         │                          │       │
      │                       │                         │                          │<──────┘
      │                       │                         │                          │
      │                       │                         │                          │ Décale texture
      │                       │                         │                          │───────┐
      │                       │                         │                          │       │
      │                       │                         │                          │<──────┘
      │                       │                         │                          │
      │                       │                         │                          │ Ajoute ligne
      │                       │                         │                          │───────┐
      │                       │                         │                          │       │
      │                       │                         │                          │<──────┘
      │                       │                         │                          │
      │                       │                         │                          │ Affiche
      │                       │                         │                          │───────┐
      │                       │                         │                          │       │
      │                       │                         │                          │<──────┘
      │                       │                         │                          │
      │                       │                         │ Retourne                 │
      │                       │                         │<─────────────────────────│
      │                       │                         │                          │
```

## 3. Implémentation pour Qt/OpenGL

### 3.1 Architecture proposée

Pour implémenter un défilement similaire dans la version Qt, il faut adapter l'approche en utilisant les fonctionnalités d'OpenGL et Qt. Voici la structure recommandée:

#### 3.1.1 Modifications à apporter à la classe AudioVisualizer

```cpp
class AudioVisualizer : public QOpenGLWidget, protected QOpenGLFunctions
{
    // ...
private:
    // Nouvelles structures de données pour le défilement
    std::vector<std::vector<unsigned char>> m_historyR;  // Historique des lignes R
    std::vector<std::vector<unsigned char>> m_historyG;  // Historique des lignes G
    std::vector<std::vector<unsigned char>> m_historyB;  // Historique des lignes B
    int m_historySize;  // Nombre de lignes dans l'historique

    // Option alternative: utiliser des textures OpenGL
    QOpenGLTexture* m_backgroundTexture;
    QOpenGLTexture* m_foregroundTexture;
    // ...
};
```

### 3.2 Méthode avec stockage d'un historique

Cette approche consiste à conserver un historique des lignes de données et à les afficher toutes à chaque rafraîchissement.

#### 3.2.1 Initialisation

```cpp
void AudioVisualizer::initializeGL()
{
    // Code existant...

    // Initialiser les structures pour l'historique
    m_historySize = 1160; // Hauteur de la fenêtre ou valeur désirée
    m_historyR.clear();
    m_historyG.clear();
    m_historyB.clear();
}
```

#### 3.2.2 Mise à jour avec défilement

```cpp
void AudioVisualizer::updateDataWithScroll(const unsigned char* r,
                                          const unsigned char* g,
                                          const unsigned char* b,
                                          int size)
{
    // Stocker les nouvelles données
    m_r.assign(r, r + size);
    m_g.assign(g, g + size);
    m_b.assign(b, b + size);
    m_dataSize = size;

    // Ajouter la nouvelle ligne au début de l'historique
    m_historyR.insert(m_historyR.begin(), std::vector<unsigned char>(r, r + size));
    m_historyG.insert(m_historyG.begin(), std::vector<unsigned char>(g, g + size));
    m_historyB.insert(m_historyB.begin(), std::vector<unsigned char>(b, b + size));

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
```

#### 3.2.3 Rendu OpenGL modifié

```cpp
void AudioVisualizer::paintGL()
{
    // Effacer l'écran
    glClear(GL_COLOR_BUFFER_BIT);

    // Vérifier si nous avons des données à afficher
    if (m_historyR.empty()) {
        return;
    }

    // Mettre à jour les données si nécessaire
    if (m_dataUpdated) {
        // Reconstruire les vertices avec l'historique des données
        m_vertices.clear();

        // Pour chaque ligne dans l'historique
        for (int y = 0; y < m_historyR.size(); ++y) {
            for (int x = 0; x < m_dataSize; ++x) {
                // Position X normalisée
                float xPos = 2.0f * x / (float)(m_dataSize - 1) - 1.0f;

                // Position Y normalisée (inversée pour que les nouvelles lignes apparaissent en haut)
                float yPos = 1.0f - 2.0f * y / (float)(m_historySize - 1);

                // Ajouter le vertex avec sa couleur
                m_vertices.push_back(xPos);                         // x
                m_vertices.push_back(yPos);                         // y
                m_vertices.push_back(m_historyR[y][x] / 255.0f);    // r
                m_vertices.push_back(m_historyG[y][x] / 255.0f);    // g
                m_vertices.push_back(m_historyB[y][x] / 255.0f);    // b
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

    // Dessiner tous les points de l'historique
    glDrawArrays(GL_POINTS, 0, m_dataSize * m_historyR.size());

    m_vao.release();
    m_program.release();
}
```

### 3.3 Méthode avec textures (plus proche de l'original)

Cette approche est plus proche de la méthode originale SFML car elle utilise deux textures pour effectuer le défilement.

#### 3.3.1 Initialisation des textures

```cpp
void AudioVisualizer::initializeGL()
{
    // Code existant...

    // Initialiser les textures
    m_backgroundTexture = new QOpenGLTexture(QOpenGLTexture::Target2D);
    m_foregroundTexture = new QOpenGLTexture(QOpenGLTexture::Target2D);

    m_backgroundTexture->create();
    m_foregroundTexture->create();

    // Définir la taille des textures
    int width = CIS_MAX_PIXELS_NB;
    int height = WINDOWS_HEIGHT; // Défini dans config.h

    m_backgroundTexture->setSize(width, height);
    m_foregroundTexture->setSize(width, height);

    // Configurer le format des textures
    m_backgroundTexture->setFormat(QOpenGLTexture::RGB8_UNorm);
    m_foregroundTexture->setFormat(QOpenGLTexture::RGB8_UNorm);

    // Allouer la mémoire
    m_backgroundTexture->allocateStorage();
    m_foregroundTexture->allocateStorage();

    // Configurer les paramètres de texture
    m_backgroundTexture->setMinificationFilter(QOpenGLTexture::Nearest);
    m_backgroundTexture->setMagnificationFilter(QOpenGLTexture::Nearest);
    m_foregroundTexture->setMinificationFilter(QOpenGLTexture::Nearest);
    m_foregroundTexture->setMagnificationFilter(QOpenGLTexture::Nearest);
}
```

#### 3.3.2 Méthode de défilement avec textures

```cpp
void AudioVisualizer::updateDataWithTextureScroll(const unsigned char* r,
                                                 const unsigned char* g,
                                                 const unsigned char* b,
                                                 int size)
{
    // Stocker les nouvelles données
    m_r.assign(r, r + size);
    m_g.assign(g, g + size);
    m_b.assign(b, b + size);
    m_dataSize = size;

    // Créer une image QImage pour la nouvelle ligne
    QImage lineImage(size, 1, QImage::Format_RGB888);
    for (int x = 0; x < size; ++x) {
        lineImage.setPixelColor(x, 0, QColor(r[x], g[x], b[x]));
    }

    makeCurrent();

    // 1. Copier la texture d'arrière-plan vers la texture de premier plan avec décalage
    QOpenGLFramebufferObject fbo(m_foregroundTexture->width(), m_foregroundTexture->height());
    fbo.bind();

    QOpenGLPaintDevice device(fbo.size());
    QPainter painter(&device);

    // Dessiner la texture d'arrière-plan avec décalage d'un pixel vers le bas
    painter.drawImage(QRect(0, 1, m_backgroundTexture->width(), m_backgroundTexture->height() - 1),
                      m_backgroundTexture->toImage());

    // Dessiner la nouvelle ligne en haut
    painter.drawImage(QRect(0, 0, size, 1), lineImage);

    painter.end();
    fbo.release();

    // Mettre à jour la texture de premier plan avec le contenu du FBO
    m_foregroundTexture->setData(fbo.toImage().convertToFormat(QImage::Format_RGB888));

    // Copier la texture de premier plan vers la texture d'arrière-plan pour la prochaine itération
    m_backgroundTexture->setData(m_foregroundTexture->toImage().convertToFormat(QImage::Format_RGB888));

    doneCurrent();

    m_dataUpdated = true;
    update();
}
```

#### 3.3.3 Rendu avec textures

```cpp
void AudioVisualizer::paintGL()
{
    // Effacer l'écran
    glClear(GL_COLOR_BUFFER_BIT);

    // Vérifier si nous avons une texture à afficher
    if (!m_foregroundTexture || !m_foregroundTexture->isCreated()) {
        return;
    }

    // Dessiner la texture de premier plan
    QOpenGLFramebufferObject* defaultFBO = QOpenGLFramebufferObject::bindDefault();
    QPainter painter(this);

    // Désactiver l'antialiasing pour un rendu pixel-perfect
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);

    // Dessiner la texture sur tout le widget
    painter.drawImage(rect(), m_foregroundTexture->toImage());

    painter.end();
}
```

### 3.4 Modification de MainWindow

Dans la classe MainWindow, il faut adapter la méthode `updateVisualization()`:

```cpp
void MainWindow::updateVisualization()
{
    // Vérifier si le double buffer contient de nouvelles données
    pthread_mutex_lock(&m_doubleBuffer->mutex);
    int dataReady = m_doubleBuffer->dataReady;
    if (dataReady) {
        m_doubleBuffer->dataReady = 0;
    }
    pthread_mutex_unlock(&m_doubleBuffer->mutex);

    if (dataReady) {
        // Mettre à jour le visualiseur avec les nouvelles données et défilement
        m_visualizer->updateDataWithScroll(  // Ou updateDataWithTextureScroll
            m_doubleBuffer->processingBuffer_R,
            m_doubleBuffer->processingBuffer_G,
            m_doubleBuffer->processingBuffer_B,
            CIS_MAX_PIXELS_NB
        );

        // ...reste du code inchangé
    }
}
```

## 4. Optimisations possibles

### 4.1 Performance

- **Réduire le nombre de vertices**: Limiter le nombre de lignes dans l'historique ou baisser la résolution verticale
- **Utiliser le GPU efficacement**: Éviter les transferts fréquents entre CPU et GPU
- **Instancing**: Utiliser le rendu par instances pour les motifs répétitifs
- **Framerate adaptatif**: Réduire la fréquence de rafraîchissement si nécessaire

### 4.2 Mémoire

- **Compression de l'historique**: Compresser les données plus anciennes
- **Pruning automatique**: Supprimer automatiquement les données trop anciennes
- **Mipmap pour l'historique lointain**: Réduire la résolution des lignes plus anciennes

### 4.3 Qualité visuelle

- **Interpolation**: Ajouter une interpolation de couleur entre les lignes pour un défilement plus fluide
- **Modes d'affichage**: Proposer différents modes (lignes/points/remplissage)
- **Effets visuels**: Ajouter des effets de "fade out" pour les lignes plus anciennes

## 5. Annexes

### 5.1 Shaders OpenGL

#### Vertex Shader modifié

```glsl
#version 330 core
layout (location = 0) in vec2 position;  // Position x,y
layout (location = 1) in vec3 color;     // Couleur RGB
out vec3 fragColor;

void main()
{
    gl_Position = vec4(position, 0.0, 1.0);
    gl_PointSize = 1.0;
    fragColor = color;
}
```

#### Fragment Shader (inchangé)

```glsl
#version 330 core
in vec3 fragColor;
out vec4 outColor;

void main()
{
    outColor = vec4(fragColor, 1.0);
}
```

### 5.2 Structure de DoubleBuffer

```c
typedef struct DoubleBuffer
{
    uint8_t *activeBuffer_R;      // Buffer R actif (écrit par le thread audio)
    uint8_t *activeBuffer_G;      // Buffer G actif
    uint8_t *activeBuffer_B;      // Buffer B actif
    uint8_t *processingBuffer_R;  // Buffer R traité (lu par le thread d'affichage)
    uint8_t *processingBuffer_G;  // Buffer G traité
    uint8_t *processingBuffer_B;  // Buffer B traité
    uint8_t dataReady;            // Flag indiquant si de nouvelles données sont disponibles
    pthread_mutex_t mutex;        // Mutex pour la synchronisation
    pthread_cond_t cond;          // Condition pour la synchronisation
} DoubleBuffer;
```

---

Document préparé par Cline, avril 2025.
