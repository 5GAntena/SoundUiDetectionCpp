#include "AudioStream.h"
#include <Windows.h>

namespace fs = std::filesystem;

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

void AudioStream::preload_noise_tracks(std::string map_choose, bool is_rain, bool is_night)
{
	std::string folder_path = "C:\\Users\\kemerios\\Desktop\\tarkov_sounds\\" + map_choose; 

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

		stereoBuffer = bored.load_wav(filename.c_str(), frames);

		noiseTrack = bored.copyBufferToVector(stereoBuffer, frames).Buffer();

		noiseVectorNormalized = noiseTrack;

		bored.normalize(noiseVectorNormalized);

		free(stereoBuffer);
	}
}

void AudioStream::AudioProcessing(float& angle, int chunkSize, float silenceThresholdDB, std::map<std::string, bool>& tarkov_maps, bool& reduction_started)
{
	Pa_ReadStream(stream_, in_buffer, BUFFER_SIZE);
	
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
				audioTrack = bored.copyBufferToVector(in_buffer, BUFFER_SIZE).Buffer();

				audioVectorNormalized = audioTrack;

				bored.normalize(audioVectorNormalized);

				//static HighPassFilter hpFilter(450.0f, SAMPLE_RATE);

				//hpFilter.processBuffer(audio_tracks);

				// 2ms runtime for NOISE_TOTAL = CHANNEL_COUNT * BUFFER_SIZE * 2

				auto start = std::chrono::high_resolution_clock::now();

				auto correlation = correlationGpu(audioVectorNormalized, noiseVectorNormalized);

				auto maxIt = std::max_element(correlation.begin(), correlation.end());
				int bestMatchIndex = std::distance(correlation.begin(), maxIt);

				FloatVector bestMatchingNoiseSegment(noiseTrack.begin() + bestMatchIndex, noiseTrack.begin() + bestMatchIndex + audioVectorNormalized.size());

				float audio_rms = bored.calculateRMS(audioTrack);

				float noise_rms = bored.calculateRMS(bestMatchingNoiseSegment);

				float scaling_factor = audio_rms / noise_rms;

				bored.scaleBuffer(bestMatchingNoiseSegment, scaling_factor);

				auto audioSegmentToTrack = InputTrack(audioTrack);
				auto noiseSegmentToTrack = InputTrack(bestMatchingNoiseSegment);

				reductionObj->ProfileNoise(noiseSegmentToTrack);

				OutputTrack outputTrack;

				reductionObj->ReduceNoise(audioSegmentToTrack, outputTrack);

				audioFinalProcessed = outputTrack.Buffer();

				auto end = std::chrono::high_resolution_clock::now();

				auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

				//std::cout << "Duration: " << duration << " ms" << std::endl;

				//bored.zeroOutLowPowerChunks(audio_proc_cache, 512);

				bored.processBuffer(audioFinalProcessed, chunkSize, silenceThresholdDB);

				FloatVector leftChannel;
				FloatVector rightChannel;

				bored.splitInterleavedStereo(audioFinalProcessed, leftChannel, rightChannel);

				auto angle_calculation = bored.calculateNeedleAngle(leftChannel, rightChannel);

				if (!angle_calculation == 0.0f)
				{
					angle = angle_calculation;
				}

				for (size_t i = 0; i < BUFFER_SIZE; i++) {
					out_buffer[i * CHANNEL_COUNT] = audioFinalProcessed[i * CHANNEL_COUNT];
					out_buffer[i * CHANNEL_COUNT + 1] = audioFinalProcessed[i * CHANNEL_COUNT + 1];
				}

				Pa_WriteStream(stream_, out_buffer, BUFFER_SIZE);
			}
		}

		if (!reduction_started || tarkov_maps["Bypass"])
		{
			//static HighPassFilter hpFilter(500.0f, SAMPLE_RATE);

			//hpFilter.processBuffer(audio_tracks);

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
			bored.addHashesBelow("Input Device found: " + std::string(inputDeviceInfo->name));
			std::cout << "Input Device found: " << inputDeviceInfo->name << std::endl;

			std::cout << "" << std::endl;
			std::cout << "Device Info" << std::endl;
			std::cout << "Device Name: " << inputDeviceInfo->name << std::endl;
			std::cout << "Device samplerate: " << inputDeviceInfo->defaultSampleRate << std::endl;
			std::cout << "Device input channels: " << inputDeviceInfo->maxInputChannels << std::endl;
			std::cout << "Device output channels: " << inputDeviceInfo->maxOutputChannels << std::endl;
			std::cout << "API: " << Pa_GetHostApiInfo(inputDeviceInfo->hostApi)->name << std::endl;
			bored.addHashesBelow("Input Device found: " + std::string(inputDeviceInfo->name));

			inputParameters.device = i;
			inputParameters.channelCount = 2;
			inputParameters.sampleFormat = paFloat32;
			inputParameters.suggestedLatency = 0;
			inputParameters.hostApiSpecificStreamInfo = NULL;
		}

		if (std::string(Pa_GetDeviceInfo(i)->name) == outputDevice && Pa_GetDeviceInfo(i)->maxOutputChannels > 0)
		{
			outputDeviceInfo = Pa_GetDeviceInfo(i);

			bored.addHashesBelow("Input Device Found: " + std::string(outputDeviceInfo->name));
			std::cout << "Input Device Found: " << outputDeviceInfo->name << std::endl;

			std::cout << "" << std::endl;
			std::cout << "Device Info" << std::endl;
			std::cout << "Device Name: " << outputDeviceInfo->name << std::endl;
			std::cout << "Device Samplerate: " << outputDeviceInfo->defaultSampleRate << std::endl;
			std::cout << "Device Input Channels: " << outputDeviceInfo->maxInputChannels << std::endl;
			std::cout << "Device Output Channels: " << outputDeviceInfo->maxOutputChannels << std::endl;
			std::cout << "API: " << Pa_GetHostApiInfo(outputDeviceInfo->hostApi)->name << std::endl;
			bored.addHashesBelow("Input Device Found: " + std::string(outputDeviceInfo->name));

			outputParameters.device = i;
			outputParameters.channelCount = 2;
			outputParameters.sampleFormat = paFloat32;
			outputParameters.suggestedLatency = 0;
			outputParameters.hostApiSpecificStreamInfo = NULL;
		}

	}
}