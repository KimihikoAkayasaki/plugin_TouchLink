#include "pch.h"
#include "TrackingHandler.h"
#include "TrackingHandler.g.cpp"

namespace winrt::DeviceHandler::implementation
{
    void TrackingHandler::Update()
    {
        // Update joints' poses here
        // Note: this is fired up every loop

        // Run the update loop
        if (initialized && statusResult == S_OK)
        {
            if (keepAlive && !ODTKRAstop && !is_ODTKRA_started)
            {
                ODTKRAThread = std::thread([this] { this->keepRiftAlive(); });
                is_ODTKRA_started = true;
            }
            else if (!keepAlive && is_ODTKRA_started)
            {
                ODTKRAstop = true;
                is_ODTKRA_started = false;
                killODT();
                ODTKRAThread.join();
                ODTKRAstop = false;
            }

            guardian->Render();

            const auto tracking_state = ovr_GetTrackingState(
                guardian->mSession, ovr_GetTimeInSeconds() +
                static_cast<float>(extraPrediction) * 0.001, ovrTrue);

            // Grab controller poses
            for (int i = 0; i <= 1; i++)
            {
                trackedJoints.at(i).Position = {
                    tracking_state.HandPoses[i].ThePose.Position.x,
                    tracking_state.HandPoses[i].ThePose.Position.y,
                    tracking_state.HandPoses[i].ThePose.Position.z
                };

                trackedJoints.at(i).Orientation = {
                    tracking_state.HandPoses[i].ThePose.Orientation.x,
                    tracking_state.HandPoses[i].ThePose.Orientation.y,
                    tracking_state.HandPoses[i].ThePose.Orientation.z,
                    tracking_state.HandPoses[i].ThePose.Orientation.w
                };

                trackedJoints.at(i).Velocity = {
                    tracking_state.HandPoses[i].LinearVelocity.x,
                    tracking_state.HandPoses[i].LinearVelocity.y,
                    tracking_state.HandPoses[i].LinearVelocity.z
                };

                trackedJoints.at(i).Acceleration = {
                    tracking_state.HandPoses[i].LinearAcceleration.x,
                    tracking_state.HandPoses[i].LinearAcceleration.y,
                    tracking_state.HandPoses[i].LinearAcceleration.z
                };

                trackedJoints.at(i).AngularVelocity = {
                    tracking_state.HandPoses[i].AngularVelocity.x,
                    tracking_state.HandPoses[i].AngularVelocity.y,
                    tracking_state.HandPoses[i].AngularVelocity.z
                };

                trackedJoints.at(i).AngularAcceleration = {
                    tracking_state.HandPoses[i].AngularAcceleration.x,
                    tracking_state.HandPoses[i].AngularAcceleration.y,
                    tracking_state.HandPoses[i].AngularAcceleration.z
                };
            }

            // Grab the HMD pose
            {
                trackedJoints.at(2).Position = {
                    tracking_state.HeadPose.ThePose.Position.x,
                    tracking_state.HeadPose.ThePose.Position.y,
                    tracking_state.HeadPose.ThePose.Position.z
                };

                trackedJoints.at(2).Orientation = {
                    tracking_state.HeadPose.ThePose.Orientation.x,
                    tracking_state.HeadPose.ThePose.Orientation.y,
                    tracking_state.HeadPose.ThePose.Orientation.z,
                    tracking_state.HeadPose.ThePose.Orientation.w
                };

                trackedJoints.at(2).Velocity = {
                    tracking_state.HeadPose.LinearVelocity.x,
                    tracking_state.HeadPose.LinearVelocity.y,
                    tracking_state.HeadPose.LinearVelocity.z
                };

                trackedJoints.at(2).Acceleration = {
                    tracking_state.HeadPose.LinearAcceleration.x,
                    tracking_state.HeadPose.LinearAcceleration.y,
                    tracking_state.HeadPose.LinearAcceleration.z
                };

                trackedJoints.at(2).AngularVelocity = {
                    tracking_state.HeadPose.AngularVelocity.x,
                    tracking_state.HeadPose.AngularVelocity.y,
                    tracking_state.HeadPose.AngularVelocity.z
                };

                trackedJoints.at(2).AngularAcceleration = {
                    tracking_state.HeadPose.AngularAcceleration.x,
                    tracking_state.HeadPose.AngularAcceleration.y,
                    tracking_state.HeadPose.AngularAcceleration.z
                };
            }

            frame++; // Hit the frame counter
        }
    }

    int32_t TrackingHandler::Initialize()
    {
        // Shutdown if already initialized
        if (initialized)
        {
            Log(L"Handler already initialized, shutting down prior to reinitialization!", 1);
            this->Shutdown(); // Try shutting down before reinitializing
        }

        // Find out the size of the buffer required to store the value
        DWORD dwBufSize = 0;
        LONG lRetVal = RegGetValue(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\WOW6432Node\\Oculus VR, LLC\\Oculus",
            L"Base",
            RRF_RT_ANY,
            nullptr,
            nullptr,
            &dwBufSize);

        if (ERROR_SUCCESS != lRetVal || dwBufSize <= 0)
            Log(L"ODT could not be found! Some things may refuse to work!", 2);

        // If we're ok
        else
        {
            std::wstring data;
            data.resize(dwBufSize / sizeof(wchar_t));

            RegGetValue(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Oculus VR, LLC\\Oculus",
                        L"Base", RRF_RT_ANY, nullptr, &data[0], &dwBufSize);

            ODTPath = data + L"Support\\oculus-diagnostics\\";
        }

        // Create a new guardian instance
        guardian = new(_aligned_malloc(sizeof(GuardianSystem), 16)) GuardianSystem(statusResult, Log);

        // Assume success
        statusResult = S_OK;

        // Setup Oculus Stuff
        guardian->start_ovr();

        // Check the yield result
        if (statusResult == S_OK) 
        {
            // Not used anymore - joints are assigned in static context
            /*for (size_t i = 0; i < guardian->vrObjects; i++)
                trackedJoints.push_back(Joint{ .Name = std::format(L"VR Object {}", i + 1).c_str() });*/
        }

        // Mark the device as initialized
        initialized = true;

        // Return the result
        return statusResult;
    }

    int32_t TrackingHandler::Shutdown()
    {
        initialized = false;

        __try
        {
            if (keepAlive)
            {
                ODTKRAstop = true;
                ODTKRAThread.join();
            }

            guardian->DIRECTX.ReleaseDevice();
            ovr_Destroy(guardian->mSession);
            ovr_Shutdown();
            delete[] guardian;

            return 0;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            [&, this]
            {
                Log(L"OVR shutdown failure!", 2);
                statusResult = R_E_INIT_FAILED;
            }();

            return -1;
        }
    }

    bool TrackingHandler::KeepAlive() const
    {
        return keepAlive;
    }

    void TrackingHandler::KeepAlive(bool value)
    {
        keepAlive = value;
    }

    bool TrackingHandler::ReduceRes() const
    {
        return resEnabled;
    }

    void TrackingHandler::ReduceRes(bool value)
    {
        resEnabled = value;
    }

    int32_t TrackingHandler::PredictionMs() const
    {
        return extraPrediction;
    }

    void TrackingHandler::PredictionMs(int32_t value)
    {
        extraPrediction = value;
    }

    bool TrackingHandler::IsInitialized() const
    {
        return initialized;
    }

    int32_t TrackingHandler::StatusResult() const
    {
        return statusResult;
    }

    event_token TrackingHandler::LogEvent(const Windows::Foundation::EventHandler<hstring>& handler)
    {
        return logEvent.add(handler);
    }

    void TrackingHandler::LogEvent(const event_token& token) noexcept
    {
        logEvent.remove(token);
    }

    com_array<Joint> TrackingHandler::TrackedJoints() const
    {
        return winrt::com_array<Joint>{trackedJoints};
    }
}
