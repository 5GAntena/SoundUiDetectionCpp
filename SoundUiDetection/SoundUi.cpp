#include "SoundUi.h"

void SoundWindow::createAppOptionsWindow(int display_w)
{
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;

    ImGui::SetNextWindowSize(ImVec2((float)display_w, 180.0f));
    ImGui::SetNextWindowPos(ImVec2(0.f, 450.0f));

    ImGui::Begin("App Options", nullptr, windowFlags);

    for (const auto& map : tarkov_maps) 
    {
        if (map.second != false)
        {
            if (ImGui::Button("Start Reduction", ImVec2(120, 20)))
            {
                std::cout << "Noise Reduction Started" << std::endl;

                redution_button_start = true;
                reduction_started = true;
                reduction_reseted = false;
            }

            break;
        }
    }

    ImGui::SetCursorPos(ImVec2(150, 27));

    if (ImGui::Button("Reset Reduction", ImVec2(120, 20)))
    {
        std::cout << "Noise Reduction Reseted" << std::endl;

        reduction_reseted = true;
        reduction_started = false;

        mNewSensitivity = 0;
        mFreqSmoothingBands = 0;
        mNoiseGain = 0;
    }

    ImGui::InputFloat("Sensitivity", &mNewSensitivity);
    ImGui::InputFloat("Smoothing Bands", &mFreqSmoothingBands);
    ImGui::InputFloat("Gain", &mNoiseGain);
    ImGui::InputInt("Chunk", &mChunkSize);
    ImGui::InputFloat("ThreshholdDB", &mSilenceThresholdDB);

    ImGui::End();
}

void SoundWindow::createMapOptionsWindow(int display_w)
{
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;

    ImGui::SetNextWindowSize(ImVec2((float)display_w, 150.0f));
    ImGui::SetNextWindowPos(ImVec2(0.f, 300.0f));

    ImGui::Begin("Map Options", nullptr, windowFlags);

    int x_coords[4] = { 8, 158, 316, 474 };
    int y_coords[4] = { 27, 50, 73, 96 };

    ImGui::SetCursorPos(ImVec2(x_coords[0], y_coords[0]));
    ImGui::Checkbox("factory", &tarkov_maps["factory"]);

    ImGui::SetCursorPos(ImVec2(x_coords[1], y_coords[0]));
    ImGui::Checkbox("outdoor", &tarkov_maps["outdoor"]);

    ImGui::SetCursorPos(ImVec2(x_coords[2], y_coords[0]));
    ImGui::Checkbox("residential", &tarkov_maps["residential"]);

    if (tarkov_maps["factory"])
    {
        ImGui::SetCursorPos(ImVec2(x_coords[0], y_coords[1]));
        ImGui::Checkbox("Bypass Audio", &tarkov_maps["Bypass"]);
    }

    if (tarkov_maps["outdoor"])
    {
        ImGui::SetCursorPos(ImVec2(x_coords[1], y_coords[1]));
        ImGui::Checkbox("rain", &tarkov_maps["rain"]);

        ImGui::SetCursorPos(ImVec2(x_coords[1], y_coords[2]));
        ImGui::Checkbox("night", &tarkov_maps["night"]);

        ImGui::SetCursorPos(ImVec2(x_coords[1], y_coords[3]));
        ImGui::Checkbox("Bypass Audio", &tarkov_maps["Bypass"]);
    }

    if (tarkov_maps["residential"])
    {
        ImGui::SetCursorPos(ImVec2(x_coords[2], y_coords[1]));
        ImGui::Checkbox("rain", &tarkov_maps["rain"]);

        ImGui::SetCursorPos(ImVec2(x_coords[2], y_coords[2]));
        ImGui::Checkbox("night", &tarkov_maps["night"]);

        ImGui::SetCursorPos(ImVec2(x_coords[2], y_coords[3]));
        ImGui::Checkbox("Bypass Audio", &tarkov_maps["Bypass"]);
    }

    for (const auto& map : tarkov_maps)
    {
        if (map.first == "Bypass" && map.second == true)
        {

            reduction_started = false;

            break;
        }

        if (map.first == "Bypass" && map.second == false)
        {
            reduction_started = true;

            break;
        }
    }

    ImGui::End();
}

void SoundWindow::createNeedleWindow(int display_w)
{
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;

    ImGui::SetNextWindowSize(ImVec2((float)display_w, 300.0f));
    ImGui::SetNextWindowPos(ImVec2(0.f, 0.f));

    ImGui::Begin("Sound Needle", nullptr, windowFlags);

    drawNeedle();

    ImGui::End();
}

void SoundWindow::drawNeedle() {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 win_size = ImGui::GetWindowSize();

    // Define the needle size and position
    float needleLength = win_size.y * 0.4f;
    float centerX = p.x + win_size.x * 0.5f;
    float centerY = p.y + win_size.y * 0.5f;

    float radians = noiceAngle * M_PI / 180.0f;

    ImGui::LabelText("degrees", std::to_string(noiceAngle).c_str());

    float needleX = centerX + needleLength * sin(radians);
    float needleY = centerY - needleLength * cos(radians);

    ImVec2 needleEnd = ImVec2(needleX, needleY);
    ImVec2 needleCenter = ImVec2(centerX, centerY);

    draw_list->AddLine(needleCenter, needleEnd, IM_COL32(255, 0, 0, 255), 2.0f);
}

void SoundWindow::Run() {
    // Poll and handle events
    glfwPollEvents();

    // Start the ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    int display_w, display_h;
    glfwGetFramebufferSize(window, &display_w, &display_h);

    createNeedleWindow(display_w);
    createMapOptionsWindow(display_w);
    createAppOptionsWindow(display_w);

    // Rendering
    ImGui::Render();

    glViewport(0, 0, display_w, display_h);
    glClearColor(0.45f, 0.55f, 0.60f, 1.00f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    // Swap buffers
    glfwSwapBuffers(window);
}