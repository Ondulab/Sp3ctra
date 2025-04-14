#ifndef AUDIOVISUALIZER_H
#define AUDIOVISUALIZER_H

#include <QOpenGLWidget>
#include <QOpenGLFunctions>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLShaderProgram>
#include <vector>

class AudioVisualizer : public QOpenGLWidget, protected QOpenGLFunctions
{
    Q_OBJECT

public:
    AudioVisualizer(QWidget *parent = nullptr);
    ~AudioVisualizer();
    
    void updateData(const unsigned char* r, const unsigned char* g, const unsigned char* b, int size);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

private:
    void setupShaders();
    void setupBuffers();
    
    QOpenGLShaderProgram m_program;
    QOpenGLBuffer m_vbo;
    QOpenGLVertexArrayObject m_vao;
    
    std::vector<float> m_vertices;
    std::vector<unsigned char> m_r;
    std::vector<unsigned char> m_g;
    std::vector<unsigned char> m_b;
    
    int m_dataSize;
    bool m_dataUpdated;
};

#endif // AUDIOVISUALIZER_H
