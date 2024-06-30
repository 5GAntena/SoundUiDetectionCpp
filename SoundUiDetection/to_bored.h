#pragma once

class BoringFunc {
public:
	void addHashesBelow(const std::string& input)
	{
		std::string hashes(input.length(), '#');
		std::cout << hashes << std::endl;
	}

	/// <summary>
	/// to be used with calculateNeedleAndle find the angle of the sound in a stereo signal 90 degrees max 
	/// </summary>
	/// <param name="channel"></param>
	/// <returns></returns>
	float calculateAverage(const std::vector<float>& channel)
	{
		float sum = 0.0f;
		for (float sample : channel)
		{

			sum += std::abs(sample);
		}
		return sum / channel.size();
	}

	float calculateNeedleAngle(const std::vector<float>& leftChannel, const std::vector<float>& rightChannel)
	{

		if (leftChannel.empty() || rightChannel.empty())
		{
			return 0.0f;
		}

		float leftLevel = calculateAverage(leftChannel);
		float rightLevel = calculateAverage(rightChannel);

		float normalizedDifference = (rightLevel - leftLevel) / (leftLevel + rightLevel);
		return normalizedDifference * 90.0f;
	}

	/// <summary>
	/// To be used idividually or with the one func bellow bufferToTracks, does what it says, splits a interleaved buffer into 2 channels or more?
	/// </summary>
	/// <param name="in"></param>
	/// <param name="channels_count"></param>
	/// <param name="channel"></param>
	/// <param name="bufferSize"></param>
	/// <returns></returns>
	InputTrack bufferToOneTrack(const float* in, int channels_count, int channel, size_t bufferSize)
	{
		FloatVector buffer(bufferSize);

		float* wptr = &buffer[0];

		for (size_t frame = 0; frame < bufferSize; frame++)
		{
			size_t index = frame * channels_count + channel;

			wptr[frame] = in[index];
		}

		return InputTrack(buffer);
	}

	std::vector<InputTrack> bufferToTracks(const float* in, int channel_count, size_t bufferSize)
	{
		std::vector<InputTrack> tracks;

		for (int channel = 0; channel < channel_count; channel++)
		{
			InputTrack track = bufferToOneTrack(in, channel_count, channel, bufferSize);
			tracks.push_back(track);
		}

		return tracks;
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

	InputTrack copyBufferToVector(const float* paBuffer, int buffersize)
	{
		std::vector<float> outputVector(buffersize * 2);
		std::copy(paBuffer, paBuffer + buffersize * 2, outputVector.begin());

		return InputTrack(outputVector);
	}


	/// <summary>
	/// This one is to be used with the one bellow processBufferWithSilence function
	/// basically takes a chunk (256, 512 etc) from a buffer of some size (2048 etc)
	/// and get the max signal db of that chunk and returns it 
	/// </summary>
	/// <param name="buffer"></param>
	/// <param name="start"></param>
	/// <param name="end"></param>
	/// <returns></returns>

	float calculateMaxdB(const std::vector<float>& buffer, size_t start, size_t end) {
		if (buffer.empty() || start >= end) {
			return -std::numeric_limits<float>::infinity();
		}

		float maxAmplitude = 0.0f;
		for (size_t i = start; i < end && i < buffer.size(); ++i) {
			float absValue = std::abs(buffer[i]);
			if (absValue > maxAmplitude) {
				maxAmplitude = absValue;
			}
		}

		if (maxAmplitude <= 0.0f) {
			return -std::numeric_limits<float>::infinity();
		}

		float maxdB = 20.0f * std::log10(maxAmplitude);

		return maxdB;
	}

	void processBufferWithSilence(std::vector<float>& buffer, size_t chunkSize, float threshold_dB) {
		size_t numChunks = buffer.size() / chunkSize;

		for (size_t chunk = 0; chunk < numChunks; ++chunk) {
			size_t startIdx = chunk * chunkSize;
			size_t endIdx = startIdx + chunkSize;

			std::cout << startIdx << std::endl;
			std::cout << endIdx << std::endl;
			std::cout << "===========" << std::endl;

			float maxdB = calculateMaxdB(buffer, startIdx, endIdx);

			if (maxdB < threshold_dB) {
				std::fill(buffer.begin() + startIdx, buffer.begin() + endIdx, 0.0f);
			}
		}
	}

	void zeroOutLowPowerChunks(std::vector<float>& buffer, size_t chunkSize) {
		const float threshold_dB = -23.f;
		const float threshold_linear = std::pow(10.0f, threshold_dB / 20.0f);

		for (size_t i = 0; i < buffer.size(); i += chunkSize) {
			float sumOfSquares = 0.0f;

			// Compute the sum of squares for the current chunk
			for (size_t j = i; j < i + chunkSize && j < buffer.size(); ++j) {
				sumOfSquares += buffer[j] * buffer[j];
			}

			float rms = std::sqrt(sumOfSquares / chunkSize);

			// Zero out the chunk if RMS is below the threshold
			if (rms < threshold_linear) {
				std::fill(buffer.begin() + i, buffer.begin() + std::min(i + chunkSize, buffer.size()), 0.0f);
			}
		}
	}
};
