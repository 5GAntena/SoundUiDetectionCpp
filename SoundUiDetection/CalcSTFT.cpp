#include "CalcSTFT.h"

std::vector<float> hann_window(int N) {
    std::vector<float> window(N);
    for (int n = 0; n < N; ++n) {
        window[n] = 0.5 - 0.5 * cos(2 * M_PI * n / N);
    }
    return window;
}

std::vector<std::complex<float>> DFT(const std::vector<float>& x) {
    int N = x.size();
    std::vector<std::complex<float>> X(N);
    for (int k = 0; k < N; ++k) {
        std::complex<float> sum(0.0, 0.0);
        for (int n = 0; n < N; ++n) {
            float angle = -2 * M_PI * k * n / N;
            sum += std::complex<float>(cos(angle), sin(angle)) * x[n];
        }
        X[k] = sum;
    }
    return X;
}

double magnitude_to_db(double magnitude, double reference = 1.0) {
    return 20.0 * log10(magnitude / reference);
}

std::vector<std::vector<std::complex<float>>> CalcSTFT::STFT(const std::vector<float>& signal, int frame_size, int hop_size) {
    std::vector<float> window = hann_window(frame_size);
    int num_frames = (signal.size() - frame_size) / hop_size + 1;
    std::vector<std::vector<std::complex<float>>> stft(num_frames, std::vector<std::complex<float>>(frame_size));

    for (int i = 0; i < num_frames; ++i) {
        std::vector<float> frame(window.begin(), window.end());
        for (int j = 0; j < frame_size; ++j) {
            frame[j] *= signal[i * hop_size + j];
        }

        std::vector<std::complex<float>> fft_result = DFT(frame);
        for (int k = 0; k < frame_size; ++k) {
            stft[i][k] = magnitude_to_db(std::abs(fft_result[k]));
        }
    }
    return stft;
}