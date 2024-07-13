#include "gpuWrapper.hpp"

__global__ static void crossCorrelateKernel(const float* noise, const float* signal, float* correlation,
    int noiseSize, int signalSize) {
    int tid = blockIdx.x * blockDim.x + threadIdx.x;
    if (tid <= noiseSize - signalSize) {
        float sum = 0.0f;
        for (int j = 0; j < signalSize; ++j) {
            sum += noise[tid + j] * signal[j];
        }
        correlation[tid] = sum;
    }
}

std::vector<float> correlationGpu(std::vector<float>& signal, std::vector<float>& noise) {
    float* d_signal, * d_noise, * d_correlation;

    cudaMalloc((void**)&d_signal, signal.size() * sizeof(float));
    cudaMalloc((void**)&d_noise, noise.size() * sizeof(float));
    cudaMalloc((void**)&d_correlation, (noise.size() - signal.size() + 1) * sizeof(float));

    cudaMemcpy(d_signal, signal.data(), signal.size() * sizeof(float), cudaMemcpyHostToDevice);
    cudaMemcpy(d_noise, noise.data(), noise.size() * sizeof(float), cudaMemcpyHostToDevice);

    int blockSize = 256;
    int numBlocks = (noise.size() - signal.size() + 1 + blockSize - 1) / blockSize;

    crossCorrelateKernel<<<numBlocks, blockSize >>>(d_noise, d_signal, d_correlation,noise.size(), signal.size());

    std::vector<float> correlation(noise.size() - signal.size() + 1);
    cudaMemcpy(correlation.data(), d_correlation, correlation.size() * sizeof(float), cudaMemcpyDeviceToHost);

    cudaFree(d_signal);
    cudaFree(d_noise);
    cudaFree(d_correlation);

    return correlation;
}