/**********************************************************************
  Audacity: A Digital Audio Editor
  NoiseReduction.cpp
  Dominic Mazzoni
  detailed rewriting by
  Paul Licameli
*******************************************************************//**
\class EffectNoiseReduction
\brief A two-pass effect to reduce background noise.
  The first pass is done over just noise.  For each windowed sample
  of the sound, we take a FFT and then statistics are tabulated for
  each frequency band.
  During the noise reduction phase, we start by setting a gain control
  for each frequency band such that if the sound has exceeded the
  previously-determined threshold, the gain is set to 0 dB, otherwise
  the gain is set lower (e.g. -18 dB), to suppress the noise.
  Then time-smoothing is applied so that the gain for each frequency
  band moves slowly, and then frequency-smoothing is applied so that a
  single frequency is never suppressed or boosted in isolation.
  Lookahead is employed; this effect is not designed for real-time
  but if it were, there would be a significant delay.
  The gain controls are applied to the complex FFT of the signal,
  and then the inverse FFT is applied.  A Hanning window may be
  applied (depending on the advanced window types setting), and then
  the output signal is then pieced together using overlap/add.
*//****************************************************************//**
*/
#include "NoiseReduction.h"
#define _USE_MATH_DEFINES   // required for msvc to define M_PI
#include <math.h>
#include <assert.h>
#include <exception>
#include <string.h>
#include <stdexcept>

#include "RealFFTf.h"
#include "Types.h"

enum DiscriminationMethod {
    DM_MEDIAN,
    DM_SECOND_GREATEST,
    DM_OLD_METHOD,

    DM_N_METHODS,
    DM_DEFAULT_METHOD = DM_SECOND_GREATEST,
};


const float minSignalTime = 0.05f;

enum WindowTypes {
    WT_RECTANGULAR_HANN = 0, // 2.0.6 behavior, requires 1/2 step
    WT_HANN_RECTANGULAR, // requires 1/2 step
    WT_HANN_HANN,        // requires 1/4 step
    WT_BLACKMAN_HANN,     // requires 1/4 step
    WT_HAMMING_RECTANGULAR, // requires 1/2 step
    WT_HAMMING_HANN, // requires 1/4 step
    WT_HAMMING_INV_HAMMING, // requires 1/2 step

    WT_N_WINDOW_TYPES,
    WT_DEFAULT_WINDOW_TYPES = WT_HANN_HANN
};

static const double DEFAULT_OLD_SENSITIVITY = 0.0;

const struct WindowTypesInfo {
    const char* name;
    unsigned minSteps;
    double inCoefficients[3];
    double outCoefficients[3];
    double productConstantTerm;
} windowTypesInfo[WT_N_WINDOW_TYPES] = {
    // In all of these cases (but the last), the constant term of the product of windows
    // is the product of the windows' two constant terms,
    // plus one half the product of the first cosine coefficients.
    { "none, Hann (2.0.6 behavior)",    2, { 1, 0, 0 },            { 0.5, -0.5, 0 }, 0.5 },
    { "Hann, none",                     2, { 0.5, -0.5, 0 },       { 1, 0, 0 },      0.5 },
    { "Hann, Hann (default)",           4, { 0.5, -0.5, 0 },       { 0.5, -0.5, 0 }, 0.375 },
    { "Blackman, Hann",                 4, { 0.42, -0.5, 0.08 },   { 0.5, -0.5, 0 }, 0.335 },
    { "Hamming, none",                  2, { 0.54, -0.46, 0.0 },   { 1, 0, 0 },      0.54 },
    { "Hamming, Hann",                  4, { 0.54, -0.46, 0.0 },   { 0.5, -0.5, 0 }, 0.385 },
    { "Hamming, Reciprocal Hamming",    2, { 0.54, -0.46, 0.0 },   { 1, 0, 0 }, 1.0 }, // output window is special
};

enum {
    DEFAULT_WINDOW_SIZE_CHOICE = 8, // corresponds to 2048
    DEFAULT_STEPS_PER_WINDOW_CHOICE = 1 // corresponds to 4, minimum for WT_HANN_HANN
};

enum NoiseReductionChoice {
    NRC_REDUCE_NOISE,
    NRC_ISOLATE_NOISE,
    NRC_LEAVE_RESIDUE,
};

class Statistics
{
public:
    Statistics(size_t spectrumSize, double rate, int windowTypes)
        : mRate(rate)
        , mWindowSize((spectrumSize - 1) * 2)
        , mWindowTypes(windowTypes)
        , mTotalWindows(0)
        , mTrackWindows(0)
        , mSums(spectrumSize)
        , mMeans(spectrumSize)
#ifdef OLD_METHOD_AVAILABLE
        , mNoiseThreshold(spectrumSize)
#endif
    {}

    // Noise profile statistics follow

    double mRate; // Rate of profile track(s) -- processed tracks must match
    size_t mWindowSize;
    int mWindowTypes;

    unsigned mTotalWindows;
    unsigned mTrackWindows;
    FloatVector mSums;
    FloatVector mMeans;

#ifdef OLD_METHOD_AVAILABLE
    // Old statistics:
    FloatVector mNoiseThreshold;
#endif
};

// This object holds information needed only during effect calculation
class NoiseReductionWorker
{
public:
    typedef NoiseReduction::Settings Settings;
    typedef Statistics Statistics;

    NoiseReductionWorker(const NoiseReduction::Settings& settings, double sampleRate
#ifdef EXPERIMENTAL_SPECTRAL_EDITING
        , double f0, double f1
#endif
    );
    NoiseReductionWorker();

    bool ProcessOne(Statistics& statistics, InputTrack& track, OutputTrack* outputTrack);

private:

    void StartNewTrack();
    void ProcessSamples(Statistics& statistics,
        float* buffer, size_t len, OutputTrack* outputTrack);
    void FillFirstHistoryWindow();
    void ApplyFreqSmoothing(FloatVector& gains);
    void GatherStatistics(Statistics& statistics);
    inline bool Classify(const Statistics& statistics, int band);
    void ReduceNoise(const Statistics& statistics, OutputTrack* outputTrack);
    void RotateHistoryWindows();
    void FinishTrackStatistics(Statistics& statistics);
    void FinishTrack(Statistics& statistics, OutputTrack* outputTrack);

private:

    const bool mDoProfile;

    const double mSampleRate;

    const size_t mWindowSize;
    // These have that size:
    HFFT     hFFT;
    FloatVector mFFTBuffer;
    FloatVector mInWaveBuffer;
    FloatVector mOutOverlapBuffer;
    // These have that size, or 0:
    FloatVector mInWindow;
    FloatVector mOutWindow;

    const size_t mSpectrumSize;
    FloatVector mFreqSmoothingScratch;
    const size_t mFreqSmoothingBins;
    // When spectral selection limits the affected band:
    int mBinLow;  // inclusive lower bound
    int mBinHigh; // exclusive upper bound

    const int mNoiseReductionChoice;
    const unsigned mStepsPerWindow;
    const size_t mStepSize;
    const int mMethod;
    const double mNewSensitivity;


    sampleCount       mInSampleCount;
    sampleCount       mOutStepCount;
    int                   mInWavePos;

    float     mOneBlockAttack;
    float     mOneBlockRelease;
    float     mNoiseAttenFactor;
    float     mOldSensitivityFactor;

    unsigned  mNWindowsToExamine;
    unsigned  mCenter;
    unsigned  mHistoryLen;

    struct Record
    {
        Record(size_t spectrumSize)
            : mSpectrums(spectrumSize)
            , mGains(spectrumSize)
            , mRealFFTs(spectrumSize - 1)
            , mImagFFTs(spectrumSize - 1)
        {
        }

        FloatVector mSpectrums;
        FloatVector mGains;
        FloatVector mRealFFTs;
        FloatVector mImagFFTs;
    };
    std::vector<movable_ptr<Record>> mQueue;
};

void NoiseReductionWorker::ApplyFreqSmoothing(FloatVector& gains)
{
    // Given an array of gain mutipliers, average them
    // GEOMETRICALLY.  Don't multiply and take nth root --
    // that may quickly cause underflows.  Instead, average the logs.

    if (mFreqSmoothingBins == 0)
        return;

    {
        float* pScratch = &mFreqSmoothingScratch[0];
        std::fill(pScratch, pScratch + mSpectrumSize, 0.0f);
    }

    for (size_t ii = 0; ii < mSpectrumSize; ++ii)
        gains[ii] = log(gains[ii]);

    for (int ii = 0; (size_t)ii < mSpectrumSize; ++ii) {
        const int j0 = std::max(0, ii - (int)mFreqSmoothingBins);
        const int j1 = std::min(mSpectrumSize - 1, ii + mFreqSmoothingBins);
        for (int jj = j0; jj <= j1; ++jj) {
            mFreqSmoothingScratch[ii] += gains[jj];
        }
        mFreqSmoothingScratch[ii] /= (j1 - j0 + 1);
    }

    for (size_t ii = 0; ii < mSpectrumSize; ++ii)
        gains[ii] = exp(mFreqSmoothingScratch[ii]);
}

NoiseReductionWorker::NoiseReductionWorker
(const NoiseReduction::Settings& settings, double sampleRate
#ifdef EXPERIMENTAL_SPECTRAL_EDITING
    , double f0, double f1
#endif
)
    : mDoProfile(settings.mDoProfile)

    , mSampleRate(sampleRate)

    , mWindowSize(settings.WindowSize())
    , hFFT(GetFFT(mWindowSize))
    , mFFTBuffer(mWindowSize)
    , mInWaveBuffer(mWindowSize)
    , mOutOverlapBuffer(mWindowSize)
    , mInWindow()
    , mOutWindow()

    , mSpectrumSize(1 + mWindowSize / 2)
    , mFreqSmoothingScratch(mSpectrumSize)
    , mFreqSmoothingBins((int)(settings.mFreqSmoothingBands))
    , mBinLow(0)
    , mBinHigh(mSpectrumSize)

    , mNoiseReductionChoice(settings.mNoiseReductionChoice)
    , mStepsPerWindow(settings.StepsPerWindow())
    , mStepSize(mWindowSize / mStepsPerWindow)
    , mMethod(settings.mMethod)

    // Sensitivity setting is a base 10 log, turn it into a natural log
    , mNewSensitivity(settings.mNewSensitivity* log(10.0))

    , mInSampleCount(0)
    , mOutStepCount(0)
    , mInWavePos(0)
{
#ifdef EXPERIMENTAL_SPECTRAL_EDITING
    {
        const double bin = mSampleRate / mWindowSize;
        if (f0 >= 0.0)
            mBinLow = floor(f0 / bin);
        if (f1 >= 0.0)
            mBinHigh = ceil(f1 / bin);
    }
#endif

    const double noiseGain = -settings.mNoiseGain;
    const unsigned nAttackBlocks = 1 + (int)(settings.mAttackTime * sampleRate / mStepSize);
    const unsigned nReleaseBlocks = 1 + (int)(settings.mReleaseTime * sampleRate / mStepSize);
    // Applies to amplitudes, divide by 20:
    mNoiseAttenFactor = DB_TO_LINEAR(noiseGain);
    // Apply to gain factors which apply to amplitudes, divide by 20:
    mOneBlockAttack = DB_TO_LINEAR(noiseGain / nAttackBlocks);
    mOneBlockRelease = DB_TO_LINEAR(noiseGain / nReleaseBlocks);
    // Applies to power, divide by 10:
    mOldSensitivityFactor = pow(10.0, settings.mOldSensitivity / 10.0);

    mNWindowsToExamine = (mMethod == DM_OLD_METHOD)
        ? std::max(2, (int)(minSignalTime * sampleRate / mStepSize))
        : 1 + mStepsPerWindow;

    mCenter = mNWindowsToExamine / 2;
    assert(mCenter >= 1); // release depends on this assumption

    if (mDoProfile)
#ifdef OLD_METHOD_AVAILABLE
        mHistoryLen = mNWindowsToExamine;
#else
        mHistoryLen = 1;
#endif
    else {
        // Allow long enough queue for sufficient inspection of the middle
        // and for attack processing
        // See ReduceNoise()
        mHistoryLen = std::max(mNWindowsToExamine, mCenter + nAttackBlocks);
    }

    mQueue.resize(mHistoryLen);
    for (unsigned ii = 0; ii < mHistoryLen; ++ii)
        mQueue[ii] = make_movable<Record>(mSpectrumSize);

    // Create windows

    const double constantTerm =
        windowTypesInfo[settings.mWindowTypes].productConstantTerm;

    // One or the other window must by multiplied by this to correct for
    // overlap.  Must scale down as steps get smaller, and overlaps larger.
    const double multiplier = 1.0 / (constantTerm * mStepsPerWindow);

    // Create the analysis window
    switch (settings.mWindowTypes) {
    case WT_RECTANGULAR_HANN:
        break;
    default:
    {
        const bool rectangularOut =
            settings.mWindowTypes == WT_HAMMING_RECTANGULAR ||
            settings.mWindowTypes == WT_HANN_RECTANGULAR;
        const double m =
            rectangularOut ? multiplier : 1;
        const double* const coefficients =
            windowTypesInfo[settings.mWindowTypes].inCoefficients;
        const double c0 = coefficients[0];
        const double c1 = coefficients[1];
        const double c2 = coefficients[2];
        mInWindow.resize(mWindowSize);
        for (size_t ii = 0; ii < mWindowSize; ++ii)
            mInWindow[ii] = m *
            (c0 + c1 * cos((2.0 * M_PI * ii) / mWindowSize)
                + c2 * cos((4.0 * M_PI * ii) / mWindowSize));
    }
    break;
    }

    if (!mDoProfile) {
        // Create the synthesis window
        switch (settings.mWindowTypes) {
        case WT_HANN_RECTANGULAR:
        case WT_HAMMING_RECTANGULAR:
            break;
        case WT_HAMMING_INV_HAMMING:
        {
            mOutWindow.resize(mWindowSize);
            for (size_t ii = 0; ii < mWindowSize; ++ii)
                mOutWindow[ii] = multiplier / mInWindow[ii];
        }
        break;
        default:
        {
            const double* const coefficients =
                windowTypesInfo[settings.mWindowTypes].outCoefficients;
            const double c0 = coefficients[0];
            const double c1 = coefficients[1];
            const double c2 = coefficients[2];
            mOutWindow.resize(mWindowSize);
            for (size_t ii = 0; ii < mWindowSize; ++ii)
                mOutWindow[ii] = multiplier *
                (c0 + c1 * cos((2.0 * M_PI * ii) / mWindowSize)
                    + c2 * cos((4.0 * M_PI * ii) / mWindowSize));
        }
        break;
        }
    }
}

void NoiseReductionWorker::StartNewTrack()
{
    float* pFill;
    for (unsigned ii = 0; ii < mHistoryLen; ++ii) {
        Record& record = *mQueue[ii];

        pFill = &record.mSpectrums[0];
        std::fill(pFill, pFill + mSpectrumSize, 0.0f);

        pFill = &record.mRealFFTs[0];
        std::fill(pFill, pFill + mSpectrumSize - 1, 0.0f);

        pFill = &record.mImagFFTs[0];
        std::fill(pFill, pFill + mSpectrumSize - 1, 0.0f);

        pFill = &record.mGains[0];
        std::fill(pFill, pFill + mSpectrumSize, mNoiseAttenFactor);
    }

    pFill = &mOutOverlapBuffer[0];
    std::fill(pFill, pFill + mWindowSize, 0.0f);

    pFill = &mInWaveBuffer[0];
    std::fill(pFill, pFill + mWindowSize, 0.0f);

    if (mDoProfile)
    {
        // We do not want leading zero padded windows
        mInWavePos = 0;
        mOutStepCount = -(int)(mHistoryLen - 1);
    }
    else
    {
        // So that the queue gets primed with some windows,
        // zero-padded in front, the first having mStepSize
        // samples of wave data:
        mInWavePos = mWindowSize - mStepSize;
        // This starts negative, to count up until the queue fills:
        mOutStepCount = -(int)(mHistoryLen - 1)
            // ... and then must pass over the padded windows,
            // before the first full window:
            - (int)(mStepsPerWindow - 1);
    }

    mInSampleCount = 0;
}

void NoiseReductionWorker::ProcessSamples
(Statistics& statistics, float* buffer, size_t len, OutputTrack* outputTrack)
{
    while (len && mOutStepCount * mStepSize < mInSampleCount) {
        auto avail = std::min(len, mWindowSize - mInWavePos);
        memmove(&mInWaveBuffer[mInWavePos], buffer, avail * sizeof(float));
        buffer += avail;
        len -= avail;
        mInWavePos += avail;

        if (mInWavePos == (int)mWindowSize) {
            FillFirstHistoryWindow();
            if (mDoProfile)
                GatherStatistics(statistics);
            else
                ReduceNoise(statistics, outputTrack);
            ++mOutStepCount;
            RotateHistoryWindows();

            // Rotate for overlap-add
            memmove(&mInWaveBuffer[0], &mInWaveBuffer[mStepSize],
                (mWindowSize - mStepSize) * sizeof(float));
            mInWavePos -= mStepSize;
        }
    }
}

void NoiseReductionWorker::FillFirstHistoryWindow()
{
    // Transform samples to frequency domain, windowed as needed
    if (mInWindow.size() > 0)
        for (size_t ii = 0; ii < mWindowSize; ++ii)
            mFFTBuffer[ii] = mInWaveBuffer[ii] * mInWindow[ii];
    else
        memmove(&mFFTBuffer[0], &mInWaveBuffer[0], mWindowSize * sizeof(float));

    RealFFTf(&mFFTBuffer[0], hFFT.get());

    Record& record = *mQueue[0];

    // Store real and imaginary parts for later inverse FFT, and compute
    // power
    {
        float* pReal = &record.mRealFFTs[1];
        float* pImag = &record.mImagFFTs[1];
        float* pPower = &record.mSpectrums[1];
        int* pBitReversed = &hFFT->BitReversed[1];
        const auto last = mSpectrumSize - 1;
        for (unsigned int ii = 1; ii < last; ++ii) {
            const int kk = *pBitReversed++;
            const float realPart = *pReal++ = mFFTBuffer[kk];
            const float imagPart = *pImag++ = mFFTBuffer[kk + 1];
            *pPower++ = realPart * realPart + imagPart * imagPart;
        }
        // DC and Fs/2 bins need to be handled specially
        const float dc = mFFTBuffer[0];
        record.mRealFFTs[0] = dc;
        record.mSpectrums[0] = dc * dc;

        const float nyquist = mFFTBuffer[1];
        record.mImagFFTs[0] = nyquist; // For Fs/2, not really imaginary
        record.mSpectrums[last] = nyquist * nyquist;
    }

    if (mNoiseReductionChoice != NRC_ISOLATE_NOISE)
    {
        // Default all gains to the reduction factor,
        // until we decide to raise some of them later
        float* pGain = &record.mGains[0];
        std::fill(pGain, pGain + mSpectrumSize, mNoiseAttenFactor);
    }
}

void NoiseReductionWorker::RotateHistoryWindows()
{
    std::rotate(mQueue.begin(), mQueue.end() - 1, mQueue.end());
}

void NoiseReductionWorker::FinishTrackStatistics(Statistics& statistics)
{
    const auto windows = statistics.mTrackWindows;
    const auto multiplier = statistics.mTotalWindows;
    const auto denom = windows + multiplier;

    // Combine averages in case of multiple profile tracks.
    if (windows)
        for (size_t ii = 0, nn = statistics.mMeans.size(); ii < nn; ++ii) {
            auto& mean = statistics.mMeans[ii];
            auto& sum = statistics.mSums[ii];
            mean = (mean * multiplier + sum) / denom;
            // Reset for next track
            sum = 0;
        }

    // Reset for next track
    statistics.mTrackWindows = 0;
    statistics.mTotalWindows = denom;
}

void NoiseReductionWorker::FinishTrack
(Statistics& statistics, OutputTrack* outputTrack)
{
    // Keep flushing empty input buffers through the history
    // windows until we've output exactly as many samples as
    // were input.
    // Well, not exactly, but not more than one step-size of extra samples
    // at the end.
    // We'll DELETE them later in ProcessOne.

    FloatVector empty(mStepSize);

    while (mOutStepCount * mStepSize < mInSampleCount) {
        ProcessSamples(statistics, &empty[0], mStepSize, outputTrack);
    }
}

void NoiseReductionWorker::GatherStatistics(Statistics& statistics)
{
    ++statistics.mTrackWindows;

    {
        // NEW statistics
        auto pPower = &mQueue[0]->mSpectrums[0];
        auto pSum = &statistics.mSums[0];
        for (size_t jj = 0; jj < mSpectrumSize; ++jj) {
            *pSum++ += *pPower++;
        }
    }

#ifdef OLD_METHOD_AVAILABLE
    // The noise threshold for each frequency is the maximum
    // level achieved at that frequency for a minimum of
    // mMinSignalBlocks blocks in a row - the max of a min.

    auto finish = mHistoryLen;

    {
        // old statistics
        auto pPower = &mQueue[0]->mSpectrums[0];
        auto pThreshold = &statistics.mNoiseThreshold[0];
        for (int jj = 0; jj < mSpectrumSize; ++jj) {
            float min = *pPower++;
            for (unsigned ii = 1; ii < finish; ++ii)
                min = std::min(min, mQueue[ii]->mSpectrums[jj]);
            *pThreshold = std::max(*pThreshold, min);
            ++pThreshold;
        }
    }
#endif
}

// Return true iff the given band of the "center" window looks like noise.
// Examine the band in a few neighboring windows to decide.
inline
bool NoiseReductionWorker::Classify(const Statistics& statistics, int band)
{
    switch (mMethod) {
#ifdef OLD_METHOD_AVAILABLE
    case DM_OLD_METHOD:
    {
        float min = mQueue[0]->mSpectrums[band];
        for (unsigned ii = 1; ii < mNWindowsToExamine; ++ii)
            min = std::min(min, mQueue[ii]->mSpectrums[band]);
        return min <= mOldSensitivityFactor * statistics.mNoiseThreshold[band];
    }
#endif
    // New methods suppose an exponential distribution of power values
    // in the noise; NEW sensitivity is meant to be log of probability
    // that noise strays above the threshold.  Call that probability
    // 1 - F.  The quantile function of an exponential distribution is
    // log (1 - F) * mean.  Thus simply multiply mean by sensitivity
    // to get the threshold.
    case DM_MEDIAN:
        // This method examines the window and all windows
        // that partly overlap it, and takes a median, to
        // avoid being fooled by up and down excursions into
        // either the mistake of classifying noise as not noise
        // (leaving a musical noise chime), or the opposite
        // (distorting the signal with a drop out).
        if (mNWindowsToExamine <= 3)
            // No different from second greatest.
            goto secondGreatest;
        else if (mNWindowsToExamine <= 5)
        {
            float greatest = 0.0, second = 0.0, third = 0.0;
            for (unsigned ii = 0; ii < mNWindowsToExamine; ++ii) {
                const float power = mQueue[ii]->mSpectrums[band];
                if (power >= greatest)
                    third = second, second = greatest, greatest = power;
                else if (power >= second)
                    third = second, second = power;
                else if (power >= third)
                    third = power;
            }
            return third <= mNewSensitivity * statistics.mMeans[band];
        }
        else {
            assert(false);
            return true;
        }
    secondGreatest:
    case DM_SECOND_GREATEST:
    {
        // This method just throws out the high outlier.  It
        // should be less prone to distortions and more prone to
        // chimes.
        float greatest = 0.0, second = 0.0;
        for (unsigned ii = 0; ii < mNWindowsToExamine; ++ii) {
            const float power = mQueue[ii]->mSpectrums[band];
            if (power >= greatest)
                second = greatest, greatest = power;
            else if (power >= second)
                second = power;
        }
        return second <= mNewSensitivity * statistics.mMeans[band];
    }
    default:
        assert(false);
        return true;
    }
}

void NoiseReductionWorker::ReduceNoise
(const Statistics& statistics, OutputTrack* outputTrack)
{
    // Raise the gain for elements in the center of the sliding history
    // or, if isolating noise, zero out the non-noise
    {
        float* pGain = &mQueue[mCenter]->mGains[0];
        if (mNoiseReductionChoice == NRC_ISOLATE_NOISE) {
            // All above or below the selected frequency range is non-noise
            std::fill(pGain, pGain + mBinLow, 0.0f);
            std::fill(pGain + mBinHigh, pGain + mSpectrumSize, 0.0f);
            pGain += mBinLow;
            for (int jj = mBinLow; jj < mBinHigh; ++jj) {
                const bool isNoise = Classify(statistics, jj);
                *pGain++ = isNoise ? 1.0 : 0.0;
            }
        }
        else {
            // All above or below the selected frequency range is non-noise
            std::fill(pGain, pGain + mBinLow, 1.0f);
            std::fill(pGain + mBinHigh, pGain + mSpectrumSize, 1.0f);
            pGain += mBinLow;
            for (int jj = mBinLow; jj < mBinHigh; ++jj) {
                const bool isNoise = Classify(statistics, jj);
                if (!isNoise)
                    *pGain = 1.0;
                ++pGain;
            }
        }
    }

    if (mNoiseReductionChoice != NRC_ISOLATE_NOISE)
    {
        // In each direction, define an exponential decay of gain from the
        // center; make actual gains the maximum of mNoiseAttenFactor, and
        // the decay curve, and their prior values.

        // First, the attack, which goes backward in time, which is,
        // toward higher indices in the queue.

        for (size_t jj = 0; jj < mSpectrumSize; ++jj) {
            for (unsigned ii = mCenter + 1; ii < mHistoryLen; ++ii) {
                const float minimum =
                    std::max(mNoiseAttenFactor,
                        mQueue[ii - 1]->mGains[jj] * mOneBlockAttack);
                float& gain = mQueue[ii]->mGains[jj];
                if (gain < minimum)
                    gain = minimum;
                else
                    // We can stop now, our attack curve is intersecting
                    // the decay curve of some window previously processed.
                    break;
            }
        }

        // Now, release.  We need only look one window ahead.  This part will
        // be visited again when we examine the next window, and
        // carry the decay further.
        {
            float* pNextGain = &mQueue[mCenter - 1]->mGains[0];
            const float* pThisGain = &mQueue[mCenter]->mGains[0];
            for (int nn = mSpectrumSize; nn--;) {
                *pNextGain =
                    std::max(*pNextGain,
                        std::max(mNoiseAttenFactor,
                            *pThisGain++ * mOneBlockRelease));
                ++pNextGain;
            }
        }
    }


    if (mOutStepCount >= -(int)(mStepsPerWindow - 1)) {
        Record& record = *mQueue[mHistoryLen - 1];  // end of the queue
        const auto last = mSpectrumSize - 1;

        if (mNoiseReductionChoice != NRC_ISOLATE_NOISE)
            // Apply frequency smoothing to output gain
            // Gains are not less than mNoiseAttenFactor
            ApplyFreqSmoothing(record.mGains);

        // Apply gain to FFT
        {
            const float* pGain = &record.mGains[1];
            const float* pReal = &record.mRealFFTs[1];
            const float* pImag = &record.mImagFFTs[1];
            float* pBuffer = &mFFTBuffer[2];
            auto nn = mSpectrumSize - 2;
            if (mNoiseReductionChoice == NRC_LEAVE_RESIDUE) {
                for (; nn--;) {
                    // Subtract the gain we would otherwise apply from 1, and
                    // negate that to flip the phase.
                    const double gain = *pGain++ - 1.0;
                    *pBuffer++ = *pReal++ * gain;
                    *pBuffer++ = *pImag++ * gain;
                }
                mFFTBuffer[0] = record.mRealFFTs[0] * (record.mGains[0] - 1.0);
                // The Fs/2 component is stored as the imaginary part of the DC component
                mFFTBuffer[1] = record.mImagFFTs[0] * (record.mGains[last] - 1.0);
            }
            else {
                for (; nn--;) {
                    const double gain = *pGain++;
                    *pBuffer++ = *pReal++ * gain;
                    *pBuffer++ = *pImag++ * gain;
                }
                mFFTBuffer[0] = record.mRealFFTs[0] * record.mGains[0];
                // The Fs/2 component is stored as the imaginary part of the DC component
                mFFTBuffer[1] = record.mImagFFTs[0] * record.mGains[last];
            }
        }

        // Invert the FFT into the output buffer
        InverseRealFFTf(&mFFTBuffer[0], hFFT.get());

        // Overlap-add
        if (mOutWindow.size() > 0) {
            float* pOut = &mOutOverlapBuffer[0];
            float* pWindow = &mOutWindow[0];
            int* pBitReversed = &hFFT->BitReversed[0];
            for (unsigned int jj = 0; jj < last; ++jj) {
                int kk = *pBitReversed++;
                *pOut++ += mFFTBuffer[kk] * (*pWindow++);
                *pOut++ += mFFTBuffer[kk + 1] * (*pWindow++);
            }
        }
        else {
            float* pOut = &mOutOverlapBuffer[0];
            int* pBitReversed = &hFFT->BitReversed[0];
            for (unsigned int jj = 0; jj < last; ++jj) {
                int kk = *pBitReversed++;
                *pOut++ += mFFTBuffer[kk];
                *pOut++ += mFFTBuffer[kk + 1];
            }
        }

        float* buffer = &mOutOverlapBuffer[0];
        if (mOutStepCount >= 0) {
            // Output the first portion of the overlap buffer, they're done
            outputTrack->Append(buffer, mStepSize);
        }

        // Shift the remainder over.
        memmove(buffer, buffer + mStepSize, sizeof(float) * (mWindowSize - mStepSize));
        std::fill(buffer + mWindowSize - mStepSize, buffer + mWindowSize, 0.0f);
    }
}

bool NoiseReductionWorker::ProcessOne(Statistics& statistics, InputTrack& inputTrack, OutputTrack* outputTrack)
{
    /**
     * Frames coming from libsndfile are striped, channel-wise: [{left, right},  {left, right}, ...]
     * NR code works on a per-track basis
     */
    StartNewTrack();

    const sf_count_t BUFFER_SIZE = 500000; // 2mb
    FloatVector buffer(BUFFER_SIZE);

    bool bLoopSuccess = true;

    for (size_t i = 0; i < inputTrack.Length();) {
        size_t len = inputTrack.Read(&buffer[0], BUFFER_SIZE);

        if (len == 0) {
            break;
        }

        i += len;
        mInSampleCount += len;
        ProcessSamples(statistics, &buffer[0], len, outputTrack);
    }

    if (bLoopSuccess) {
        if (mDoProfile)
            FinishTrackStatistics(statistics);
        else
            FinishTrack(statistics, outputTrack);
    }

    if (bLoopSuccess && !mDoProfile) {
        // Filtering effects always end up with more data than they started with.  Delete this 'tail'.
        outputTrack->SetEnd(inputTrack.Length());
    }

    return bLoopSuccess;
}

NoiseReduction::NoiseReduction(NoiseReduction::Settings& settings, double sampleRate) :
    mSettings(settings),
    mSampleRate(sampleRate)
{
    size_t spectrumSize = 1 + mSettings.WindowSize() / 2;
    mStatistics.reset(new Statistics(spectrumSize, mSampleRate, mSettings.mWindowTypes));
}

// found out why destructor is important:
// otherwise error with unique_ptr because Statistics is incomplete type
// also important to define destructor here, not directly in header, because Statistics needs to be defined
NoiseReduction::~NoiseReduction() = default;

void NoiseReduction::ProfileNoise(InputTrack& profileTrack) {

    NoiseReduction::Settings profileSettings(mSettings);
    profileSettings.mDoProfile = true;
    NoiseReductionWorker profileWorker(profileSettings, mSampleRate);

    if (!profileWorker.ProcessOne(*this->mStatistics, profileTrack, nullptr)) {
        throw std::runtime_error("Cannot process track");
    }

    if (this->mStatistics->mTotalWindows == 0) {
        throw std::invalid_argument("Selected noise profile is too short.");
    }
}

void NoiseReduction::ReduceNoise(InputTrack& inputTrack, OutputTrack& outputTrack) {

    NoiseReduction::Settings cleanSettings(mSettings);
    cleanSettings.mDoProfile = false;
    NoiseReductionWorker cleanWorker(cleanSettings, mSampleRate);

    if (!cleanWorker.ProcessOne(*this->mStatistics, inputTrack, &outputTrack)) {
        throw std::runtime_error("Cannot process track");
    }
}

NoiseReduction::Settings::Settings() {
    mDoProfile = false;

    mWindowTypes = WT_DEFAULT_WINDOW_TYPES;
    mWindowSizeChoice = DEFAULT_WINDOW_SIZE_CHOICE;
    mStepsPerWindowChoice = DEFAULT_STEPS_PER_WINDOW_CHOICE;
    mMethod = DM_DEFAULT_METHOD;
    mOldSensitivity = DEFAULT_OLD_SENSITIVITY;
    mNoiseReductionChoice = NRC_REDUCE_NOISE;

    mNewSensitivity = 6.0;
    mNoiseGain = 25.0;
    mAttackTime = 0.02;
    mReleaseTime = 0.10;
    mFreqSmoothingBands = 0;
}