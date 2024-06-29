#pragma once

#define _USE_MATH_DEFINES

#include <iostream>
#include <vector>
#include <complex>
#include <cmath>
#include <math.h>

class CalcSTFT
{
public:

    std::vector<std::vector<std::complex<float>>> STFT(const std::vector<float>& signal, int frame_size, int hop_size);
};
