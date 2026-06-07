#ifndef GPU_FFT_OCEAN_H
#define GPU_FFT_OCEAN_H

#include "../graphics/Shader.h"

#include <glm/glm.hpp>

class GPUFFTOcean {
public:
    GPUFFTOcean(unsigned int resolution, float oceanSize, float windSpeed, float windAngleDegrees, float choppiness);
    ~GPUFFTOcean();

    void update(float deltaTime);

    unsigned int getResolution() const { return m_resolution; }
    float getOceanSize() const { return m_oceanSize; }
    unsigned int getDisplacementTexture() const { return m_displacementTexture; }
    unsigned int getNormalTexture() const { return m_normalTexture; }

    // Runtime-tunable knobs (no rebuild needed)
    float getHeightScale()     const { return m_heightScale; }
    float getHorizontalScale() const { return m_horizontalScale; }
    float getChoppiness()      const { return m_choppiness; }
    float getTimeScale()       const { return m_timeScale; }
    float getWindSpeed()       const { return m_windSpeed; }

    void setHeightScale(float v)     { m_heightScale     = v; }
    void setHorizontalScale(float v) { m_horizontalScale = v; }
    void setChoppiness(float v)      { m_choppiness      = v; }
    void setTimeScale(float v)       { m_timeScale       = v; }
    void setWindSpeed(float v);

private:
    unsigned int m_resolution;
    unsigned int m_log2Resolution;
    float m_oceanSize;
    float m_windSpeed;
    float m_windAngleDegrees;
    float m_choppiness;
    float m_heightScale;
    float m_horizontalScale;
    float m_timeScale;
    glm::vec2 m_windDirection;
    unsigned int m_currentPhaseIndex;

    Shader m_initialSpectrumShader;
    Shader m_phaseShader;
    Shader m_spectrumShader;
    Shader m_fftHorizontalShader;
    Shader m_fftVerticalShader;
    Shader m_displacementShader;
    Shader m_normalShader;

    unsigned int m_gaussianNoiseTexture;
    unsigned int m_initialSpectrumTexture;
    unsigned int m_phaseTextures[2];
    unsigned int m_spectrumTextureA;
    unsigned int m_spectrumTextureB;
    unsigned int m_intermediateTextureA;
    unsigned int m_intermediateTextureB;
    unsigned int m_spatialTextureA;
    unsigned int m_spatialTextureB;
    unsigned int m_displacementTexture;
    unsigned int m_normalTexture;

    // Stockham FFT ping-pong bookkeeping: track which texture pair holds the
    // result after the row pass and after the final (column) pass.
    unsigned int m_fftRowResultA = 0;
    unsigned int m_fftRowResultB = 0;
    unsigned int m_fftFinalA = 0;
    unsigned int m_fftFinalB = 0;

    void initializeTextures();
    void initializeNoiseTexture();
    void buildInitialSpectrum();
    void configureTexture(unsigned int texture, GLenum internalFormat) const;
};

#endif
