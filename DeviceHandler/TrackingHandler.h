#pragma once
#include "TrackingHandler.g.h"
#include "GuardianSystem.h"

namespace winrt::DeviceHandler::implementation
{
    struct TrackingHandler : TrackingHandlerT<TrackingHandler>
    {
        TrackingHandler() = default;

        void Update();
        int32_t Initialize();
        int32_t Shutdown();

        [[nodiscard]] bool KeepAlive() const;
        void KeepAlive(bool value);

        [[nodiscard]] bool ReduceRes() const;
        void ReduceRes(bool value);

        [[nodiscard]] int32_t PredictionMs() const;
        void PredictionMs(int32_t value);

        [[nodiscard]] bool IsInitialized() const;
        [[nodiscard]] int32_t StatusResult() const;

        event_token LogEvent(const Windows::Foundation::EventHandler<hstring>& handler);
        void LogEvent(const event_token& token) noexcept;

        [[nodiscard]] com_array<Joint> TrackedJoints() const;

    private:
        event<Windows::Foundation::EventHandler<hstring>> logEvent;

        std::function<void(std::wstring, int32_t)> Log = std::bind(
            &TrackingHandler::LogMessage, this, std::placeholders::_1, std::placeholders::_2);

        bool initialized = false;
        HRESULT statusResult = R_E_NOT_STARTED;

        std::vector<Joint> trackedJoints = {
            Joint{.Name = L"Left Touch Controller"},
            Joint{.Name = L"Right Touch Controller"},
            Joint{.Name = L"Oculus VR Headset"}
        };

        std::thread ODTKRAThread;
        GuardianSystem* guardian;

        unsigned int frame = 0;
        bool is_ODTKRA_started = false;
        bool ODTKRAstop = false;

        // Message logging handler: bound to <Log>
        void LogMessage(const std::wstring& message, const int32_t& severity)
        {
            logEvent(*this, std::format(L"[{}] ", severity) + message);
        }

        // RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Oculus VR,
        // LLC\\Oculus", L"Base", RRF_RT_ANY, NULL, (PVOID)&value, &BufferSize);
        std::wstring ODTPath = L"Test";

        int extraPrediction = 11;
        bool keepAlive = false;
        bool resEnabled = true;

        void killODT() const
        {
            // Reverse ODT cli commands
            std::wstring temp = std::format(
                L"echo service set-pixels-per-display-pixel-override 1 | \"{}OculusDebugToolCLI.exe\"", ODTPath);
            ShellExecute(NULL, L"cmd.exe", temp.c_str(), NULL, NULL, SW_HIDE);

            if (HWND hWindowHandle =
                    FindWindow(NULL, L"Oculus Debug Tool");
                hWindowHandle != NULL)
            {
                SendMessage(hWindowHandle, WM_CLOSE, 0, 0);
                SwitchToThisWindow(hWindowHandle, true);
            }
            Sleep(500);
        }


        // Check if ODT is running
        // Returns false if not, and true if it is
        bool check_ODT()
        {
            auto Target_window_Name = L"Oculus Debug Tool";
            HWND hWindowHandle = FindWindow(NULL, Target_window_Name);

            if (hWindowHandle == NULL)
                return false;
            return true;
        }

        void start_ODT(HWND& hWindowHandle, LPCWSTR& Target_window_Name)
        {
            // Starts ODT
            std::wstring tempstr = ODTPath + L"OculusDebugTool.exe";
            ShellExecute(NULL, L"open", tempstr.c_str(), NULL, NULL, SW_SHOWDEFAULT);
            Sleep(1000); // not sure if needed

            hWindowHandle = FindWindow(NULL, Target_window_Name);
            SwitchToThisWindow(hWindowHandle, true);

            Sleep(100); // not sure if needed

            // Goes to the "Bypass Proximity Sensor Check" toggle
            for (int i = 0; i < 7; i++)
            {
                keybd_event(VK_DOWN, 0xE0, KEYEVENTF_EXTENDEDKEY | 0, 0);
                keybd_event(VK_DOWN, 0xE0, KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP, 0);
            }

            ShowWindow(hWindowHandle, SW_MINIMIZE);
        }

        void ODT_CLI()
        {
            //Sets "set-pixels-per-display-pixel-override" to 0.01 to decrease performance overhead
            std::wstring temp = std::format(
                L"echo service set-pixels-per-display-pixel-override 0.01 | \"{}OculusDebugToolCLI.exe\"", ODTPath);
            ShellExecute(NULL, L"cmd.exe", temp.c_str(), NULL, NULL, SW_HIDE);

            //Turn off ASW, we do not need it
            temp = std::format(L"echo server: asw.Off | \"{}OculusDebugToolCLI.exe\"", ODTPath);
            ShellExecute(NULL, L"cmd.exe", temp.c_str(), NULL, NULL, SW_HIDE);
        }

        void keepRiftAlive()
        {
            int seconds = 600000;

            auto Target_window_Name = L"Oculus Debug Tool";
            HWND hWindowHandle = FindWindow(nullptr, Target_window_Name);

            if (hWindowHandle != nullptr)
            {
                SendMessage(hWindowHandle, WM_CLOSE, 0, 0);
                SwitchToThisWindow(hWindowHandle, true);
            }

            Sleep(500); // not sure if needed

            //Sends commands to the Oculus Debug Tool CLI to decrease performance overhead
            //Unlikely to do much, but no reason not to.
            if (resEnabled)
                ODT_CLI();

            //Starts Oculus Debug Tool
            //std::string temp = ODTPath + "OculusDebugTool.exe";
            //TestOutput->Text(std::wstring(temp.begin(), temp.end()));

            start_ODT(hWindowHandle, Target_window_Name);

            Sleep(1000);

            if (check_ODT() == false)
            {
                is_ODTKRA_started = false;
                return;
            }


            hWindowHandle = FindWindow(nullptr, Target_window_Name);

            HWND PropertGrid = FindWindowEx(hWindowHandle, nullptr, L"wxWindowNR", nullptr);
            HWND wxWindow = FindWindowEx(PropertGrid, nullptr, L"wxWindow", nullptr);

            while (!ODTKRAstop)
            {
                if (seconds == 600000)
                {
                    SendMessage(wxWindow, WM_KEYDOWN, VK_UP, 0);
                    SendMessage(wxWindow, WM_KEYUP, VK_UP, 0);
                    Sleep(50);
                    SendMessage(wxWindow, WM_KEYDOWN, VK_DOWN, 0);
                    SendMessage(wxWindow, WM_KEYUP, VK_DOWN, 0);
                    seconds = 0;
                }

                seconds++;
                Sleep(1000);
            }

            ODTKRAstop = false;
        }
    };
}

namespace winrt::DeviceHandler::factory_implementation
{
    struct TrackingHandler : TrackingHandlerT<TrackingHandler, implementation::TrackingHandler>
    {
    };
}
