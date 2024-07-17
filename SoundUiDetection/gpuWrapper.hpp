#pragma once
#include <cuda_runtime.h>
#include <device_launch_parameters.h>

#include <vector>

std::vector<float> correlationGpu(std::vector<float>& signal, std::vector<float>& noise);

std::vector<float> correlationGpuOther(std::vector<float>& signal, std::vector<float>& noise);