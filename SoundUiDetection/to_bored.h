#pragma once

class BoringFunc {
public:
	void addHashesBelow(const std::string& input)
	{
		std::string hashes(input.length(), '#');
		std::cout << hashes << std::endl;
	}

	/// <summary>
	/// Do you have a stereo buffer split into left and right channel and you want it to be inteleaved again, yeah
	/// </summary>
	/// <param name="leftChannel"></param>
	/// <param name="rightChannel"></param>
	/// <returns>float* array</returns>
	float* interleaveChannels(const std::vector<float>& leftChannel, const std::vector<float>& rightChannel) {
		if (leftChannel.size() != rightChannel.size())
		{
			std::cerr << "Error: Channels have different sizes!" << std::endl;
			std::cout << leftChannel.size() << " " << rightChannel.size() << std::endl;
		}

		float* interleavedBuffer = new float[leftChannel.size() * 2];

		for (size_t i = 0; i < leftChannel.size(); ++i)
		{
			interleavedBuffer[2 * i] = leftChannel[i];
			interleavedBuffer[2 * i + 1] = rightChannel[i];
		}

		return interleavedBuffer;
	}

	void splitInterleavedStereo(const std::vector<float>& interleaved, std::vector<float>& leftChannel, std::vector<float>& rightChannel) {
		size_t totalSamples = interleaved.size();
		if (totalSamples % 2 != 0) {
			throw std::invalid_argument("Interleaved vector must have an even number of elements.");
		}

		size_t numSamplesPerChannel = totalSamples / 2;

		// Resize the output vectors to hold the samples for each channel
		leftChannel.resize(numSamplesPerChannel);
		rightChannel.resize(numSamplesPerChannel);

		// Split the interleaved vector into left and right channels
		for (size_t i = 0; i < numSamplesPerChannel; ++i) {
			leftChannel[i] = interleaved[2 * i];      // Left channel sample
			rightChannel[i] = interleaved[2 * i + 1]; // Right channel sample
		}
	}

	/// <summary>
	/// What the name says get the filename path and read the wav into a float* buffer
	/// </summary>
	/// <param name="filename"></param>
	/// <param name="frames"></param>
	/// <returns>float* buffer</returns>

	float* load_wav(const char* filename, sf_count_t& frames) {

		SF_INFO sfinfo;

		SNDFILE* sndfile = sf_open(filename, SFM_READ, &sfinfo);
		if (sndfile == NULL) {
			std::cerr << "Failed to open file: " << filename << std::endl;
			return nullptr;
		}

		frames = sfinfo.frames;

		float* buffer = (float*)malloc(frames * 2 * sizeof(float));
		if (buffer == NULL) {
			std::cerr << "Failed to allocate memory for the audio buffer." << std::endl;
			sf_close(sndfile);
			return nullptr;
		}

		sf_count_t num_samples = sf_readf_float(sndfile, buffer, frames);

		if (num_samples != frames) {
			std::cerr << "Did not read expected amount of frames." << std::endl;
			free(buffer);
			sf_close(sndfile);
			return nullptr;
		}

		sf_close(sndfile);
		return buffer;
	}

	InputTrack copyBufferToVector(const float* paBuffer, unsigned long buffersize)
	{
		std::vector<float> outputVector(buffersize * 2);
		std::copy(paBuffer, paBuffer + buffersize * 2, outputVector.begin());

		return InputTrack(outputVector);
	}

	float calculateRMS(const std::vector<float>& buffer) {
		float sum_of_squares = std::accumulate(buffer.begin(), buffer.end(), 0.0f,
			[](float sum, float value) { return sum + value * value; });
		return std::sqrt(sum_of_squares / buffer.size());
	}

	float calculateNeedleAngle(const std::vector<float>& leftChannel, const std::vector<float>& rightChannel)
	{
		if (leftChannel.empty() || rightChannel.empty())
		{
			return 0.0f;
		}

		float leftRMS = calculateRMS(leftChannel);
		float rightRMS = calculateRMS(rightChannel);

		float sumRMS = leftRMS + rightRMS;

		if (sumRMS == 0.0f)
		{
			return 0.0f;
		}

		float normalizedDifference = (rightRMS - leftRMS) / sumRMS;

		return normalizedDifference * 90.0f;
	}

	void scaleBuffer(std::vector<float>& buffer, float scaling_factor) {
		for (auto& sample : buffer) {
			sample *= scaling_factor;
		}
	}

	float calculateChunkMaxDB(const std::vector<float>& chunk) {
		if (chunk.empty()) {
			return -std::numeric_limits<float>::infinity();
		}

		float peakAmplitude = *std::max_element(chunk.begin(), chunk.end(),
			[](float a, float b) { return std::fabs(a) < std::fabs(b); });

		float maxDB = 20.0f * std::log10(std::fabs(peakAmplitude));

		return maxDB;
	}

	void processBuffer(std::vector<float>& buffer, size_t chunkSize = 512, float silenceThresholdDB = -46.0f) {
		if (buffer.empty() || chunkSize == 0) {
			return;
		}

		//const float silenceThresholdLinear = std::pow(10.0f, silenceThresholdDB / 20.0f);

		size_t numChunks = (buffer.size() + chunkSize - 1) / chunkSize;
		std::vector<float> maxDBs(numChunks);

		std::for_each(std::execution::par, maxDBs.begin(), maxDBs.end(), [&](float& chunkMaxDB) {
			size_t chunkIndex = &chunkMaxDB - maxDBs.data();
			size_t start = chunkIndex * chunkSize;

			std::vector<float> chunk(buffer.begin() + start, buffer.begin() + std::min(start + chunkSize, buffer.size()));

			chunkMaxDB = calculateChunkMaxDB(chunk);
			});

		float meanMaxDB = std::accumulate(maxDBs.begin(), maxDBs.end(), 0.0f) / maxDBs.size();

		if (meanMaxDB < silenceThresholdDB) {
			std::for_each(std::execution::par, buffer.begin(), buffer.end(), [](float& sample) {
				sample = 0.0f;
				});
		}
	}

	float mean(const std::vector<float>& data) {
		float sum = 0;
		for (float value : data) {
			sum += value;
		}
		return sum / data.size();
	}

	float standardDeviation(const std::vector<float>& data, float mean) {
		float sum = 0;
		for (float value : data) {
			sum += (value - mean) * (value - mean);
		}
		return std::sqrt(sum / data.size());
	}

	void normalize(std::vector<float>& data) {
		float dataMean = mean(data);
		float dataStdDev = standardDeviation(data, dataMean);
		for (float& value : data) {
			value = (value - dataMean) / dataStdDev;
		}
	}
};

class HighPassFilter 
{
public:
	HighPassFilter(float cutoffFrequency, float sampleRate)
	{
		float RC = 1.0f / (2.0f * M_PI * cutoffFrequency);
		dt = 1.0f / sampleRate;
		alpha = RC / (RC + dt);

		prevInputL = 0.0f;
		prevOutputL = 0.0f;
		prevInputR = 0.0f;
		prevOutputR = 0.0f;
	}

	void processBuffer(std::vector<float>& buffer)
	{
		for (size_t i = 0; i < buffer.size(); i += 2)
		{
			// Process left channel
			float inputL = buffer[i];
			float outputL = alpha * (prevOutputL + inputL - prevInputL);
			prevInputL = inputL;
			prevOutputL = outputL;
			buffer[i] = outputL;

			// Process right channel
			float inputR = buffer[i + 1];
			float outputR = alpha * (prevOutputR + inputR - prevInputR);
			prevInputR = inputR;
			prevOutputR = outputR;
			buffer[i + 1] = outputR;
		}
	}

private:
	float alpha;
	float dt;
	float prevInputL;
	float prevOutputL;
	float prevInputR;
	float prevOutputR;
};

class LowPassFilter 
{
public:
	LowPassFilter(float cutoffFrequency, float sampleRate) 
	{
		float RC = 1.0f / (2.0f * M_PI * cutoffFrequency);
		dt = 1.0f / sampleRate;
		alpha = dt / (RC + dt);

		prevOutputL = 0.0f;
		prevOutputR = 0.0f;
	}

	void processBuffer(std::vector<float>& buffer) 
	{
		for (size_t i = 0; i < buffer.size(); i += 2) 
		{
			// Process left channel
			float inputL = buffer[i];
			float outputL = alpha * inputL + (1.0f - alpha) * prevOutputL;
			prevOutputL = outputL;
			buffer[i] = outputL;

			// Process right channel
			float inputR = buffer[i + 1];
			float outputR = alpha * inputR + (1.0f - alpha) * prevOutputR;
			prevOutputR = outputR;
			buffer[i + 1] = outputR;
		}
	}

private:
	float alpha;
	float dt;
	float prevOutputL;
	float prevOutputR;
};

class BandPassFilter 
{
public:
	BandPassFilter(float lowCutoffFrequency, float highCutoffFrequency, float sampleRate) 
	{
		float lowRC = 1.0f / (2.0f * M_PI * lowCutoffFrequency);
		float highRC = 1.0f / (2.0f * M_PI * highCutoffFrequency);
		dt = 1.0f / sampleRate;

		alphaLow = dt / (lowRC + dt);
		alphaHigh = highRC / (highRC + dt);

		prevInputLowL = 0.0f;
		prevOutputLowL = 0.0f;
		prevInputLowR = 0.0f;
		prevOutputLowR = 0.0f;

		prevOutputHighL = 0.0f;
		prevOutputHighR = 0.0f;
	}

	void processBuffer(std::vector<float>& buffer) 
	{
		for (size_t i = 0; i < buffer.size(); i += 2) 
		{
			float inputL = buffer[i];

			float lowOutputL = alphaLow * inputL + (1.0f - alphaLow) * prevOutputLowL;
			prevOutputLowL = lowOutputL;

			float bandPassOutputL = alphaHigh * (prevOutputHighL + lowOutputL - prevInputLowL);
			prevInputLowL = lowOutputL;
			prevOutputHighL = bandPassOutputL;

			buffer[i] = bandPassOutputL;

			float inputR = buffer[i + 1];

			float lowOutputR = alphaLow * inputR + (1.0f - alphaLow) * prevOutputLowR;
			prevOutputLowR = lowOutputR;

			float bandPassOutputR = alphaHigh * (prevOutputHighR + lowOutputR - prevInputLowR);
			prevInputLowR = lowOutputR;
			prevOutputHighR = bandPassOutputR;

			buffer[i + 1] = bandPassOutputR;
		}
	}

private:
	float alphaLow;
	float alphaHigh;
	float dt;

	float prevInputLowL;
	float prevOutputLowL;
	float prevInputLowR;
	float prevOutputLowR;

	float prevOutputHighL;
	float prevOutputHighR;
};
