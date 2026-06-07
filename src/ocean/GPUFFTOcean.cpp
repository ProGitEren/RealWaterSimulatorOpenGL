#include "GPUFFTOcean.h"

#include <glm/common.hpp>

#include <iostream>
#include <cmath>
#include <random>
#include <vector>

namespace {
    constexpr unsigned int kMaxResolution = 1024;
    constexpr float kPi = 3.14159265359f;

    unsigned int nextPowerOfTwoLog(unsigned int value) {
        unsigned int result = 0;
        while ((1u << result) < value) {
            ++result;
        }
        return result;
    }
}

GPUFFTOcean::GPUFFTOcean(unsigned int resolution, float oceanSize, float windSpeed, float windAngleDegrees, float choppiness)
    : m_resolution(resolution),
      m_log2Resolution(nextPowerOfTwoLog(resolution)),
      m_oceanSize(oceanSize),
      m_windSpeed(windSpeed),
      m_windAngleDegrees(windAngleDegrees),
      m_choppiness(choppiness),
      m_heightScale(1.0f),        // 1/N normalization in displacement shader; modest scale
      m_horizontalScale(0.08f),
      m_timeScale(2.0f),
      m_windDirection(glm::normalize(glm::vec2(std::cos(glm::radians(windAngleDegrees)), std::sin(glm::radians(windAngleDegrees))))),
      m_currentPhaseIndex(0),
      m_initialSpectrumShader("../assets/shaders/fft_initial_spectrum.comp"),
      m_phaseShader("../assets/shaders/fft_phase.comp"),
      m_spectrumShader("../assets/shaders/fft_spectrum.comp"),
      m_fftHorizontalShader("../assets/shaders/fft_horizontal.comp"),
      m_fftVerticalShader("../assets/shaders/fft_vertical.comp"),
      m_displacementShader("../assets/shaders/fft_displacement.comp"),
      m_normalShader("../assets/shaders/fft_normal.comp"),
      m_gaussianNoiseTexture(0),
      m_initialSpectrumTexture(0),
      m_phaseTextures{ 0, 0 },
      m_spectrumTextureA(0),
      m_spectrumTextureB(0),
      m_intermediateTextureA(0),
      m_intermediateTextureB(0),
      m_spatialTextureA(0),
      m_spatialTextureB(0),
      m_displacementTexture(0),
      m_normalTexture(0) {
    // Stockham FFT ping-pongs across log2(N) CPU-driven passes, so there is no
    // single-workgroup shared-memory cap. Any power-of-two up to kMaxResolution.
    if (resolution == 0 || (resolution & (resolution - 1u)) != 0u || resolution > kMaxResolution) {
        std::cerr << "GPUFFTOcean requires a power-of-two resolution up to " << kMaxResolution << std::endl;
    }

    initializeTextures();
    initializeNoiseTexture();
    buildInitialSpectrum();
}

GPUFFTOcean::~GPUFFTOcean() {
    glDeleteTextures(1, &m_gaussianNoiseTexture);
    glDeleteTextures(1, &m_initialSpectrumTexture);
    glDeleteTextures(2, m_phaseTextures);
    glDeleteTextures(1, &m_spectrumTextureA);
    glDeleteTextures(1, &m_spectrumTextureB);
    glDeleteTextures(1, &m_intermediateTextureA);
    glDeleteTextures(1, &m_intermediateTextureB);
    glDeleteTextures(1, &m_spatialTextureA);
    glDeleteTextures(1, &m_spatialTextureB);
    glDeleteTextures(1, &m_displacementTexture);
    glDeleteTextures(1, &m_normalTexture);
}

void GPUFFTOcean::configureTexture(unsigned int texture, GLenum internalFormat) const {
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexStorage2D(GL_TEXTURE_2D, 1, internalFormat, static_cast<GLsizei>(m_resolution), static_cast<GLsizei>(m_resolution));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

void GPUFFTOcean::initializeTextures() {
    glGenTextures(1, &m_gaussianNoiseTexture);
    glGenTextures(1, &m_initialSpectrumTexture);
    glGenTextures(2, m_phaseTextures);
    glGenTextures(1, &m_spectrumTextureA);
    glGenTextures(1, &m_spectrumTextureB);
    glGenTextures(1, &m_intermediateTextureA);
    glGenTextures(1, &m_intermediateTextureB);
    glGenTextures(1, &m_spatialTextureA);
    glGenTextures(1, &m_spatialTextureB);
    glGenTextures(1, &m_displacementTexture);
    glGenTextures(1, &m_normalTexture);

    configureTexture(m_gaussianNoiseTexture, GL_RG32F);
    configureTexture(m_initialSpectrumTexture, GL_RG32F);
    configureTexture(m_phaseTextures[0], GL_R32F);
    configureTexture(m_phaseTextures[1], GL_R32F);
    configureTexture(m_spectrumTextureA, GL_RGBA32F);
    configureTexture(m_spectrumTextureB, GL_RG32F);
    configureTexture(m_intermediateTextureA, GL_RGBA32F);
    configureTexture(m_intermediateTextureB, GL_RG32F);
    configureTexture(m_spatialTextureA, GL_RGBA32F);
    configureTexture(m_spatialTextureB, GL_RG32F);
    configureTexture(m_displacementTexture, GL_RGBA32F);
    configureTexture(m_normalTexture, GL_RGBA16F);

    std::mt19937 rng(2026u);
    std::uniform_real_distribution<float> phaseDistribution(0.0f, 2.0f * kPi);
    std::vector<float> initialPhase(m_resolution * m_resolution, 0.0f);
    for (float& phase : initialPhase) {
        phase = phaseDistribution(rng);
    }

    glBindTexture(GL_TEXTURE_2D, m_phaseTextures[0]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(m_resolution), static_cast<GLsizei>(m_resolution), GL_RED, GL_FLOAT, initialPhase.data());
    glBindTexture(GL_TEXTURE_2D, m_phaseTextures[1]);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(m_resolution), static_cast<GLsizei>(m_resolution), GL_RED, GL_FLOAT, initialPhase.data());
}

void GPUFFTOcean::initializeNoiseTexture() {
    std::mt19937 rng(1337u);
    std::normal_distribution<float> gaussian(0.0f, 1.0f);
    std::vector<glm::vec2> noise(m_resolution * m_resolution);

    for (glm::vec2& sample : noise) {
        sample.x = gaussian(rng);
        sample.y = gaussian(rng);
    }

    glBindTexture(GL_TEXTURE_2D, m_gaussianNoiseTexture);
    glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        0,
        0,
        static_cast<GLsizei>(m_resolution),
        static_cast<GLsizei>(m_resolution),
        GL_RG,
        GL_FLOAT,
        noise.data()
    );
}

void GPUFFTOcean::buildInitialSpectrum() {
    const unsigned int workgroups = (m_resolution + 15u) / 16u;

    m_initialSpectrumShader.use();
    m_initialSpectrumShader.setUInt("resolution", m_resolution);
    m_initialSpectrumShader.setFloat("oceanSize", m_oceanSize);
    m_initialSpectrumShader.setFloat("windSpeed", m_windSpeed);
    m_initialSpectrumShader.setVec2("windDirection", m_windDirection);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_gaussianNoiseTexture);
    m_initialSpectrumShader.setInt("gaussianNoiseTexture", 0);
    glBindImageTexture(0, m_initialSpectrumTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG32F);
    glDispatchCompute(workgroups, workgroups, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
}

void GPUFFTOcean::setWindSpeed(float v) {
    m_windSpeed = glm::clamp(v, 5.0f, 10.0f);
    buildInitialSpectrum();
}

void GPUFFTOcean::update(float deltaTime) {
    const unsigned int workgroups = (m_resolution + 15u) / 16u;
    const unsigned int nextPhaseIndex = 1u - m_currentPhaseIndex;

    m_phaseShader.use();
    m_phaseShader.setUInt("resolution", m_resolution);
    m_phaseShader.setFloat("oceanSize", m_oceanSize);
    m_phaseShader.setFloat("deltaTime", deltaTime);
    m_phaseShader.setFloat("timeScale", m_timeScale);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_phaseTextures[m_currentPhaseIndex]);
    m_phaseShader.setInt("currentPhaseTexture", 0);
    glBindImageTexture(0, m_phaseTextures[nextPhaseIndex], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_R32F);
    glDispatchCompute(workgroups, workgroups, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    m_currentPhaseIndex = nextPhaseIndex;

    m_spectrumShader.use();
    m_spectrumShader.setUInt("resolution", m_resolution);
    m_spectrumShader.setFloat("oceanSize", m_oceanSize);
    m_spectrumShader.setFloat("choppiness", m_choppiness);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_initialSpectrumTexture);
    m_spectrumShader.setInt("initialSpectrumTexture", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_phaseTextures[m_currentPhaseIndex]);
    m_spectrumShader.setInt("phaseTexture", 1);
    glBindImageTexture(0, m_spectrumTextureA, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    glBindImageTexture(1, m_spectrumTextureB, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG32F);
    glDispatchCompute(workgroups, workgroups, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    // ---- Stockham FFT: ping-pong log2(N) passes per axis (no shared-mem cap) ----
    // Two ping-pong buffer pairs: (intermediate*) and (spatial*). The first
    // horizontal pass reads from (spectrum*); thereafter we alternate.
    // Each butterfly thread handles 2 elements, so launch N/2 threads per line.
    const unsigned int halfN = m_resolution / 2u;

    // ROWS (horizontal). local size = (256, 1); launch ceil(N/2 / 256) x N groups.
    m_fftHorizontalShader.use();
    m_fftHorizontalShader.setInt("resolution", static_cast<int>(m_resolution));
    {
        unsigned int srcA = m_spectrumTextureA, srcB = m_spectrumTextureB;
        unsigned int dstA = m_intermediateTextureA, dstB = m_intermediateTextureB;
        unsigned int altA = m_spatialTextureA, altB = m_spatialTextureB;
        const unsigned int groupsXh = (halfN + 255u) / 256u;
        for (unsigned int p = 1u; p < m_resolution; p <<= 1u) {
            m_fftHorizontalShader.setInt("subseqCount", static_cast<int>(p));
            glBindImageTexture(0, srcA, 0, GL_FALSE, 0, GL_READ_ONLY,  GL_RGBA32F);
            glBindImageTexture(1, srcB, 0, GL_FALSE, 0, GL_READ_ONLY,  GL_RG32F);
            glBindImageTexture(2, dstA, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
            glBindImageTexture(3, dstB, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG32F);
            glDispatchCompute(groupsXh, m_resolution, 1);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
            // ping-pong: dst becomes next src; reuse the other pair as new dst
            srcA = dstA; srcB = dstB;
            dstA = (dstA == m_intermediateTextureA) ? altA : m_intermediateTextureA;
            dstB = (dstB == m_intermediateTextureB) ? altB : m_intermediateTextureB;
        }
        // After the loop, the final horizontal result lives in srcA/srcB.
        m_fftRowResultA = srcA; m_fftRowResultB = srcB;
    }

    // COLS (vertical). local size = (1, 256).
    m_fftVerticalShader.use();
    m_fftVerticalShader.setInt("resolution", static_cast<int>(m_resolution));
    {
        unsigned int srcA = m_fftRowResultA, srcB = m_fftRowResultB;
        // pick a destination pair distinct from src
        unsigned int dstA = (srcA == m_spatialTextureA) ? m_intermediateTextureA : m_spatialTextureA;
        unsigned int dstB = (srcB == m_spatialTextureB) ? m_intermediateTextureB : m_spatialTextureB;
        const unsigned int groupsYv = (halfN + 255u) / 256u;
        for (unsigned int p = 1u; p < m_resolution; p <<= 1u) {
            m_fftVerticalShader.setInt("subseqCount", static_cast<int>(p));
            glBindImageTexture(0, srcA, 0, GL_FALSE, 0, GL_READ_ONLY,  GL_RGBA32F);
            glBindImageTexture(1, srcB, 0, GL_FALSE, 0, GL_READ_ONLY,  GL_RG32F);
            glBindImageTexture(2, dstA, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
            glBindImageTexture(3, dstB, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RG32F);
            glDispatchCompute(m_resolution, groupsYv, 1);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
            unsigned int tmpA = srcA, tmpB = srcB;
            srcA = dstA; srcB = dstB; dstA = tmpA; dstB = tmpB;
        }
        // Final spatial result -> copy into the canonical spatial textures if needed.
        m_fftFinalA = srcA; m_fftFinalB = srcB;
    }

    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    m_displacementShader.use();
    m_displacementShader.setUInt("resolution", m_resolution);
    m_displacementShader.setFloat("heightScale", m_heightScale);
    m_displacementShader.setFloat("horizontalScale", m_horizontalScale);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_fftFinalA);   // wherever the ping-pong landed
    m_displacementShader.setInt("spatialTextureA", 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_fftFinalB);
    m_displacementShader.setInt("spatialTextureB", 1);
    glBindImageTexture(0, m_displacementTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
    glDispatchCompute(workgroups, workgroups, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    m_normalShader.use();
    m_normalShader.setUInt("resolution", m_resolution);
    m_normalShader.setFloat("oceanSize", m_oceanSize);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_displacementTexture);
    m_normalShader.setInt("displacementTexture", 0);
    glBindImageTexture(0, m_normalTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA16F);
    glDispatchCompute(workgroups, workgroups, 1);
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
}
