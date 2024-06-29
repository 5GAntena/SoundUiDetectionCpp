#pragma once
#include <portaudio.h>
#include <iostream>
#include <string>
#include <filesystem>
#include <vector>
#include <fstream>
#include <sndfile.h>
#include <map>
#include <numeric>

#include "InputTrack.h"
#include "OutputTrack.h"
#include "NoiseReduction.h"
#include "CalcSTFT.h"

class AudioStream
{
public:
	AudioStream(const NoiseReduction::Settings& settings) :
		noiseReductionSettings(settings)
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

	void AudioProcessing(float &angle, std::map<std::string, bool>& tarkov_maps, bool& reduction_started);

	void findInputDeviceIndex();

private:
	double SAMPLE_RATE = 44100;
	size_t BUFFER_SIZE = 2048;
	int CHANNEL_COUNT = 2;
	size_t NOISE_TOTAL = CHANNEL_COUNT * BUFFER_SIZE;

	float* in_buffer = (float*)malloc(BUFFER_SIZE * 2 * sizeof(float));
	float* out_buffer = (float*)malloc(BUFFER_SIZE * 2 * sizeof(float));

	PaStreamParameters inputParameters;
	PaStreamParameters outputParameters;

	float* stereoBuffer;
	sf_count_t frames;
	const char* filename;

	std::vector<std::string> noise_paths;
	std::vector<std::vector<InputTrack>> noise_tracks;
	std::vector<std::vector<InputTrack>> noise_cache;

	std::vector<float*> noiseArray;
	int noise_index = 0;
	bool preload = false;

	std::vector<InputTrack> audio_tracks;
	std::vector<InputTrack> audio_cache;
	std::vector<InputTrack> audio_proc_cache;

	std::vector<OutputTrack> outputTracks;

	std::vector<float> leftChannel;
	std::vector<float> rightChannel;

	bool mapChoosen = false;

	void gatherNoiseSamples(float* buffer);
	void gatherNoiseTracks(size_t framesPerBuffer);
	void preload_noise_tracks(std::string map_choose, bool is_rain, bool is_night);
	void file_path_getter(std::string map_choose, bool is_rain, bool is_night);

	PaStream* stream_;
	NoiseReduction::Settings noiseReductionSettings;
	CalcSTFT calc;
};