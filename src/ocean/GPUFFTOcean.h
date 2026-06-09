#ifndef GPU_FFT_OCEAN_H
#define GPU_FFT_OCEAN_H

#include "../graphics/Shader.h"

#include <glm/glm.hpp>
#include <vector>

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
    void setWindAngle(float degrees);                       // rebuilds the spectrum
    float getWindAngle() const { return m_windAngleDegrees; }

    // Spectrum knobs (rebuild the initial spectrum on change)
    float getSeaMaturity() const { return m_seaMaturity; }
    float getAmplitude()   const { return m_amplitude; }
    void  setSeaMaturity(float v);
    void  setAmplitude(float v);

    // CPU-side ocean-surface sampling (Phase 0B). The displacement texture is
    // read back to the CPU once per update(); sampleOceanHeight returns the
    // vertical wave height (metres) at a world XZ, matching the GPU surface.
    void      readbackDisplacement();   // call ONCE per render frame (not per substep)
    glm::vec3 sampleDisplacement(float worldX, float worldZ) const;
    float     sampleOceanHeight(float worldX, float worldZ) const;
    glm::vec3 sampleOceanNormal(float worldX, float worldZ) const;

    // Choppiness-correct surface query: the FFT pushes vertices sideways
    // (horizontal displacement), so the surface point that VISUALLY appears at
    // world (X,Z) came from a different grid point. These invert that mapping
    // (fixed-point iteration) so floating objects sit ON the wave, not beside
    // it — the error otherwise grows with wave size / choppiness.
    float     sampleSurfaceHeight(float worldX, float worldZ) const;
    glm::vec3 sampleSurfaceNormal(float worldX, float worldZ) const;
    double    getLastReadbackMs() const { return m_lastReadbackMs; }

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
    float m_seaMaturity;
    float m_amplitude;
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

    // CPU readback of the displacement texture: .x=dx, .y=height, .z=dz (metres).
    std::vector<glm::vec4> m_cpuDisplacement;
    double m_lastReadbackMs = 0.0;
    unsigned int m_pbo[2] = { 0, 0 };   // double-buffered async readback (ping-pong)
    unsigned int m_pboIndex = 0;
    unsigned int m_readbackCount = 0;

    void initializeTextures();
    void initializeNoiseTexture();
    void buildInitialSpectrum();
    void configureTexture(unsigned int texture, GLenum internalFormat) const;
};

#endif
