#include "SoundUi.h"
#include "AudioStream.h"
#include "NoiseReduction.h"
#include <iostream>

int main(int argc, char** argv)
{
	SoundWindow* uiWindow = nullptr;
	AudioStream* audioStream = nullptr;
	NoiseReduction* reductionObj = nullptr;

	uiWindow = new SoundWindow();

	while (!glfwWindowShouldClose(uiWindow->window))
	{
		if (uiWindow->redution_button_start)
		{
			NoiseReduction::Settings settings;

			settings.mNewSensitivity = uiWindow->mNewSensitivity;
			settings.mFreqSmoothingBands = uiWindow->mFreqSmoothingBands;
			settings.mNoiseGain = uiWindow->mNoiseGain;

			reductionObj = new NoiseReduction(settings, 44100);

			std::cout << "Settings imported" << std::endl;

			audioStream = new AudioStream(reductionObj, 44100);

			if (!audioStream->initStreamObj() || !audioStream->openStream() || !audioStream->startStream())
			{
				return 1;
			}

			std::cout << "Audio Stream Started" << std::endl;

			uiWindow->redution_button_start = false;
		}

		if (uiWindow->reduction_reseted)
		{
			reductionObj->~NoiseReduction();
			reductionObj = nullptr;

;			audioStream->~AudioStream();
			audioStream = nullptr;

			for (auto& map : uiWindow->tarkov_maps)
			{
				map.second = false;
			}

			uiWindow->mNewSensitivity = 6.0f;
			uiWindow->mFreqSmoothingBands = 6.0f;
			uiWindow->mNoiseGain = 10.f;
			uiWindow->noiceAngle = 0.0f;

			uiWindow->reduction_reseted = false;
			uiWindow->redution_button_start = true;
		}

		uiWindow->Run();

		if (audioStream != nullptr)
		{
			audioStream->AudioProcessing(uiWindow->noiceAngle, uiWindow->tarkov_maps, uiWindow->reduction_started);
		}
	}

	return 0;
}