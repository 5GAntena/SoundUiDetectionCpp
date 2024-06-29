#include "SoundUi.h"
#include "AudioStream.h"
#include "NoiseReduction.h"
#include <iostream>

int main(int argc, char** argv)
{
	SoundWindow* uiWindow = nullptr;

	AudioStream* audioStream = nullptr;

	uiWindow = new SoundWindow();

	while (!glfwWindowShouldClose(uiWindow->window))
	{
		if (uiWindow->redution_button_start)
		{
			NoiseReduction::Settings settings;

			settings.mNewSensitivity = uiWindow->mNewSensitivity;
			settings.mFreqSmoothingBands = uiWindow->mFreqSmoothingBands;
			settings.mNoiseGain = uiWindow->mNoiseGain;

			std::cout << "Settings imported" << std::endl;

			audioStream = new AudioStream(settings);

			if (!audioStream->initStreamObj() || !audioStream->openStream() || !audioStream->startStream())
			{
				return 1;
			}

			std::cout << "Audio Stream Started" << std::endl;

			uiWindow->redution_button_start = false;
		}

		if (uiWindow->reduction_reseted)
		{
			audioStream->~AudioStream();
			audioStream = nullptr;

			for (auto& map : uiWindow->tarkov_maps)
			{
				map.second = false;
			}

			uiWindow->mNewSensitivity = 6.f;
			uiWindow->mFreqSmoothingBands = 0.f;
			uiWindow->mNoiseGain = 25.f;
			uiWindow->noiceAngle = 0.f;

			uiWindow->reduction_started = false;
			uiWindow->reduction_reseted = false;
		}

		uiWindow->Run();

		if (audioStream != nullptr)
		{
			audioStream->AudioProcessing(uiWindow->noiceAngle, uiWindow->tarkov_maps, uiWindow->reduction_started);
		}
	}

	return 0;
}