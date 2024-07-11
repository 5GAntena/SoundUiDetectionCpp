#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#define _USE_MATH_DEFINES
#include <cmath>
#include <stdexcept>

#define GL_SILENCE_DEPRECATION
#if defined(IMGUI_IMPL_OPENGL_ES2)
#include <GLES2/gl2.h>
#endif
#include <GLFW/glfw3.h>

#include <iostream>
#include <string>
#include <map>

class SoundWindow {
public:
    SoundWindow(){
        if (!glfwInit())
            throw std::runtime_error("Failed to initialize GLFW");

        window = glfwCreateWindow(600, 700, "Needle", NULL, NULL);

        glfwMakeContextCurrent(window);
        glfwSwapInterval(1);

        // Setup Dear ImGui context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        ImGui::StyleColorsDark();

        // Setup Platform/Renderer bindings
        ImGui_ImplGlfw_InitForOpenGL(window, true);
        ImGui_ImplOpenGL3_Init("#version 130");

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
    }

    ~SoundWindow() {
        // Cleanup
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        glfwDestroyWindow(window);
        glfwTerminate();
    }

    void Run();

    GLFWwindow* window;

    std::map<std::string, bool> tarkov_maps = {
        { "factory", false },
        { "outdoor", false },
        { "residential", false },
        { "rain", false },
        { "night", false },
        { "Bypass", false }
    };

    float mNewSensitivity = 6.0f;
    float mFreqSmoothingBands = 6.0f;
    float mNoiseGain = 10.f;
    float noiceAngle = 0.0f;

    int mChunkSize = 512;
    float mSilenceThresholdDB = -46.0f;

    bool reduction_started = false;
    bool reduction_reseted = false;
    bool redution_button_start = false;

private:

    void createNeedleWindow(int display_w);
    void createMapOptionsWindow(int display_w);
    void createAppOptionsWindow(int display_W);

    void drawNeedle();
};