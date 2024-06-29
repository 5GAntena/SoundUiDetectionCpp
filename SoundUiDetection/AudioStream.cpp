#include "AudioStream.h"
#include <Windows.h>

namespace fs = std::filesystem;

void addHashesBelow(const std::string& input)
{
	std::string hashes(input.length(), '#');
	std::cout << hashes << std::endl;
}

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

void AudioStream::file_path_getter(std::string folder_path, bool is_rain, bool is_night)
{
	for (const auto& entry : fs::directory_iterator(folder_path))
	{
		if (fs::is_regular_file(entry.status()))
		{
			std::string file_path = entry.path().filename().string();

			if (file_path.find("rain") == std::string::npos && file_path.find("thunder") == std::string::npos && file_path.find("night") == std::string::npos)
			{
				noise_paths.push_back(folder_path + "\\" + file_path);
			}

			if (is_rain && (file_path.find("rain") != std::string::npos || file_path.find("thunder") != std::string::npos))
			{
				noise_paths.push_back(folder_path + "\\" + file_path);
			}
			
			if (is_night && file_path.find("night") != std::string::npos)
			{
				noise_paths.push_back(folder_path + "\\" + file_path);
			}
		}
	}
}

void AudioStream::gatherNoiseSamples(float* buffer)
{
	size_t chunkSamples = BUFFER_SIZE * 2;

	size_t numberOfChunks = NOISE_TOTAL / BUFFER_SIZE;

	for (size_t i = 0; i < numberOfChunks; ++i)
	{
		float* chunk = new float[chunkSamples];

		std::copy(buffer + i * chunkSamples, buffer + (i + 1) * chunkSamples, chunk);
		
		noiseArray.push_back(chunk);
	}
}

void AudioStream::gatherNoiseTracks(size_t framesPerBuffer)
{
	for (const auto& buffer : noiseArray)
	{
		auto tracks = bufferToTracks(buffer, CHANNEL_COUNT, framesPerBuffer);

		noise_tracks.push_back(tracks);
	}
}

void AudioStream::preload_noise_tracks(std::string map_choose, bool is_rain, bool is_night)
{
	// change the folder path to your folder path
	std::string folder_path = "..\\..\\Desktop\\tarkov_sounds\\" + map_choose;

	if (map_choose == "factory")
	{
		if (!fs::exists(folder_path) || !fs::is_directory(folder_path))
		{
			std::cout << "Folder does not exists" << std::endl;
		}

		file_path_getter(folder_path, is_rain, is_night);
	}

	if (map_choose == "outdoor")
	{
		std::cout << "outdoor selected" << std::endl;

		if (!fs::exists(folder_path) || !fs::is_directory(folder_path))
		{
			std::cout << "Folder does not exists" << std::endl;
		}

		file_path_getter(folder_path, is_rain, is_night);
	}

	if (map_choose == "residential")
	{
		if (!fs::exists(folder_path) || !fs::is_directory(folder_path))
		{
			std::cout << "Folder does not exists" << std::endl;
		}

		file_path_getter(folder_path, is_rain, is_night);
	}

	for (const auto& filename : noise_paths)
	{
		std::cout << filename << std::endl;

		stereoBuffer = load_wav(filename.c_str(), frames);

		gatherNoiseSamples(stereoBuffer);

		gatherNoiseTracks(BUFFER_SIZE);

		noise_cache.insert(noise_cache.end(), noise_tracks.begin(), noise_tracks.end());

		free(stereoBuffer);
	}
}

void AudioStream::AudioProcessing(float& angle, std::map<std::string, bool>& tarkov_maps, bool& reduction_started)
{
	Pa_ReadStream(stream_, in_buffer, BUFFER_SIZE);

	NoiseReduction reduction(noiseReductionSettings, SAMPLE_RATE);

	if (in_buffer != NULL)
	{
		if (reduction_started)
		{
			if (preload == false)
			{
				for (const auto& map : tarkov_maps)
				{
					if (map.second == true)
					{
						mapChoosen = map.second;

						preload_noise_tracks(map.first, tarkov_maps["rain"], tarkov_maps["night"]);

						break;
					}
				}

				preload = true;
			}

			if (mapChoosen)
			{
				audio_tracks = bufferToTracks(in_buffer, CHANNEL_COUNT, BUFFER_SIZE);
				audio_cache.insert(audio_cache.end(), audio_tracks.begin(), audio_tracks.end());

				for (auto& tracksVector : noise_cache)
				{
					outputTracks.clear();
					audio_proc_cache.clear();

					for (auto& tracksInputTrack : tracksVector)
					{
						reduction.ProfileNoise(tracksInputTrack);
					}

					for (auto& audio_track : audio_cache)
					{
						OutputTrack outputTrack;
						reduction.ReduceNoise(audio_track, outputTrack);

						outputTracks.push_back(outputTrack);
					}

					audio_cache.clear();

					for (auto& proc : outputTracks)
					{
						audio_proc_cache.push_back(InputTrack(proc.Buffer()));
					}

					audio_cache.insert(audio_cache.end(), audio_proc_cache.begin(), audio_proc_cache.end());
				}

				const std::vector<float>& leftChannel = audio_cache[0].Buffer();
				const std::vector<float>& rightChannel = audio_cache[1].Buffer();

				//float* interleavedBuffer = interleaveChannels(leftChannel, rightChannel);

				/*std::vector<float> interleaved_vector = std::vector<float>(interleavedBuffer, interleavedBuffer + leftChannel.size());

				auto stft_result = calc.STFT(interleaved_vector, 2048, 1024);

				for (size_t i = 0; i < stft_result.size(); ++i) {
					float max_db = -std::numeric_limits<float>::infinity();
					for (size_t k = 0; k < stft_result[i].size(); ++k) {
						if (stft_result[i][k].real() > max_db) {
							max_db = stft_result[i][k].real();
						}
					}

					if (max_db > 15.f)
					{
						angle = calculateNeedleAngle(leftChannel, rightChannel);
					}
				}*/

				angle = calculateNeedleAngle(leftChannel, rightChannel);

				for (size_t i = 0; i < BUFFER_SIZE; i++) {
					out_buffer[i * 2] = leftChannel[i];
					out_buffer[i * 2 + 1] = rightChannel[i];
				}

				Pa_WriteStream(stream_, out_buffer, BUFFER_SIZE);

				noise_cache.clear();
				noise_cache.insert(noise_cache.end(), noise_tracks.begin(), noise_tracks.end());

				audio_tracks.clear();
				audio_cache.clear();
				audio_proc_cache.clear();
				outputTracks.clear();
			}
		}
		else
		{

			angle = calculateNeedleAngle(leftChannel, rightChannel);

			for (size_t i = 0; i < BUFFER_SIZE; i++) {
				out_buffer[i * 2] = in_buffer[i * 2];
				out_buffer[i * 2 + 1] = in_buffer[i * 2 + 1];
			}

			Pa_WriteStream(stream_, out_buffer, BUFFER_SIZE);
		}
	}
}

bool AudioStream::initStreamObj()
{
	PaError err = Pa_Initialize();

	if (err != paNoError)
	{
		std::cout << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;

		return false;
	}
	return true;
}

bool AudioStream::openStream()
{
	findInputDeviceIndex();

	if (inputParameters.device < 0)
	{
		Pa_Terminate();
		return false;
	}

	PaError err = Pa_OpenStream(&stream_, &inputParameters, &outputParameters, SAMPLE_RATE, BUFFER_SIZE, paClipOff, nullptr, this);

	if (err != paNoError)
	{
		std::cout << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
		Pa_Terminate();
		return false;
	}
	else
	{
		std::cout << "" << std::endl;
		std::cout << "Stream Opened" << std::endl;
		return true;
	}
}

bool AudioStream::startStream()
{
	PaError err = Pa_StartStream(stream_);

	if (err != paNoError)
	{
		std::cout << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
		return false;
	}
	else
	{
		std::cout << "" << std::endl;
		std::cout << "Stream Started" << std::endl;
		return true;
	}
}

void AudioStream::closeStream()
{
	PaError err = Pa_CloseStream(stream_);

	if (err != paNoError)
	{
		std::cout << "PortAudio error: " << Pa_GetErrorText(err) << std::endl;
	}
	else
	{
		std::cout << "" << std::endl;
		std::cout << "Stream Closed" << std::endl;
	}
}

void AudioStream::findInputDeviceIndex()
{
	int numDevices = Pa_GetDeviceCount();

	if (numDevices < 0)
	{
		std::cout << "Error: PortAudio failed to get device count" << std::endl;
	}

	std::string inputDevice = "CABLE Output (VB-Audio Virtual Cable)";
	std::string outputDevice = "Voicemeeter Input (VB-Audio Voi";

	const PaDeviceInfo* inputDeviceInfo = Pa_GetDeviceInfo(Pa_GetDefaultInputDevice());
	const PaDeviceInfo* outputDeviceInfo = nullptr;

	for (int i = 0; i < numDevices; ++i)
	{

		if (inputDeviceInfo && std::string(Pa_GetDeviceInfo(i)->name) == std::string(inputDeviceInfo->name) && inputDeviceInfo->maxInputChannels > 0)
		{
			addHashesBelow("Input Device found: " + std::string(inputDeviceInfo->name));
			std::cout << "Input Device found: " << inputDeviceInfo->name << std::endl;

			std::cout << "" << std::endl;
			std::cout << "Device Info" << std::endl;
			std::cout << "Device Name: " << inputDeviceInfo->name << std::endl;
			std::cout << "Device samplerate: " << inputDeviceInfo->defaultSampleRate << std::endl;
			std::cout << "Device input channels: " << inputDeviceInfo->maxInputChannels << std::endl;
			std::cout << "Device output channels: " << inputDeviceInfo->maxOutputChannels << std::endl;
			std::cout << "API: " << Pa_GetHostApiInfo(inputDeviceInfo->hostApi)->name << std::endl;
			addHashesBelow("Input Device found: " + std::string(inputDeviceInfo->name));

			inputParameters.device = i;
			inputParameters.channelCount = 2;
			inputParameters.sampleFormat = paFloat32;
			inputParameters.suggestedLatency = 0;
			inputParameters.hostApiSpecificStreamInfo = NULL;
		}

		if (std::string(Pa_GetDeviceInfo(i)->name) == outputDevice && Pa_GetDeviceInfo(i)->maxOutputChannels > 0)
		{
			outputDeviceInfo = Pa_GetDeviceInfo(i);

			addHashesBelow("Input Device Found: " + std::string(outputDeviceInfo->name));
			std::cout << "Input Device Found: " << outputDeviceInfo->name << std::endl;

			std::cout << "" << std::endl;
			std::cout << "Device Info" << std::endl;
			std::cout << "Device Name: " << outputDeviceInfo->name << std::endl;
			std::cout << "Device Samplerate: " << outputDeviceInfo->defaultSampleRate << std::endl;
			std::cout << "Device Input Channels: " << outputDeviceInfo->maxInputChannels << std::endl;
			std::cout << "Device Output Channels: " << outputDeviceInfo->maxOutputChannels << std::endl;
			std::cout << "API: " << Pa_GetHostApiInfo(outputDeviceInfo->hostApi)->name << std::endl;
			addHashesBelow("Input Device Found: " + std::string(outputDeviceInfo->name));

			outputParameters.device = i;
			outputParameters.channelCount = 2;
			outputParameters.sampleFormat = paFloat32;
			outputParameters.suggestedLatency = 0;
			outputParameters.hostApiSpecificStreamInfo = NULL;
		}

	}
}