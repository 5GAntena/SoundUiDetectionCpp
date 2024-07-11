#pragma once

#define _USE_MATH_DEFINES

#include <portaudio.h>
#include <iostream>
#include <string>
#include <filesystem>
#include <vector>
#include <fstream>
#include <sndfile.h>
#include <map>
#include <cmath>
#include <numeric>
#include <execution>

#include "InputTrack.h"
#include "OutputTrack.h"
#include "NoiseReduction.h"

#include "to_bored.h"

class AudioStream
{
public:
	AudioStream(NoiseReduction* reductionObj, float sample_rate) 
		: reductionObj(reductionObj), SAMPLE_RATE(sample_rate)
	{

	}

	~AudioStream()
	{
		if (stream_)
		{
			Pa_StopStream(stream_);
			Pa_CloseStream(stream_);
			Pa_Terminate();
		}
	}

	bool initStreamObj();

	bool openStream();

	bool startStream();

	void closeStream();

	void AudioProcessing(float& angle, int chunkSize, float silenceThresholdDB, std::map<std::string, bool>& tarkov_maps, bool& reduction_started);

	void findInputDeviceIndex();

private:
	float SAMPLE_RATE;
	size_t BUFFER_SIZE = 2048;
	int CHANNEL_COUNT = 2;

	size_t NOISE_TOTAL = CHANNEL_COUNT * BUFFER_SIZE * 2;

	float* in_buffer = (float*)malloc(BUFFER_SIZE * 2 * sizeof(float));
	float* out_buffer = (float*)malloc(BUFFER_SIZE * 2 * sizeof(float));

	PaStreamParameters inputParameters;
	PaStreamParameters outputParameters;

	float* stereoBuffer;
	sf_count_t frames;
	const char* filename;

	std::vector<std::string> noise_paths;
	std::vector<InputTrack> noise_tracks;
	std::vector<InputTrack> noise_cache;

	std::vector<float*> noiseArray;
	int noise_index = 0;
	bool preload = false;

	FloatVector audio_tracks;
	FloatVector audio_cache;
	FloatVector audio_proc_cache;

	bool mapChoosen = false;

	void gatherNoiseSamples(float* buffer);
	void gatherNoiseTracks(size_t framesPerBuffer);
	void preload_noise_tracks(std::string map_choose, bool is_rain, bool is_night);
	void file_path_getter(std::string map_choose, bool is_rain, bool is_night);

	NoiseReduction* reductionObj;
	PaStream* stream_;
	BoringFunc bored;
};