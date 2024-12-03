// Copyright (c) Microsoft Corporation. All rights reserved.
// Licensed under the MIT License. See LICENSE in the project root for license information.

using System;
using System.Collections.ObjectModel;
using System.ComponentModel.Composition;
using System.Linq;
using System.Numerics;
using System.Threading;
using System.Timers;
using Amethyst.Plugins.Contract;
using DeviceHandler;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using DQuaternion = DeviceHandler.Quaternion;
using DVector = DeviceHandler.Vector;
using DJoint = DeviceHandler.Joint;
using Quaternion = System.Numerics.Quaternion;

// To learn more about WinUI, the WinUI project structure,
// and more about our project templates, see: http://aka.ms/winui-project-info.

namespace plugin_TouchLink;

[Export(typeof(ITrackingDevice))]
[ExportMetadata("Name", "TouchLink")]
[ExportMetadata("Guid", "DELTANRU-VEND-APII-DVCE-DVCEOTCHLINK")]
[ExportMetadata("Publisher", "JamJar, DeltaNeverUsed")]
[ExportMetadata("Version", "1.0.0.3")]
[ExportMetadata("Website", "https://github.com/KimihikoAkayasaki/plugin_TouchLink")]
public class TouchLink : ITrackingDevice
{
    [Import(typeof(IAmethystHost))] private IAmethystHost Host { get; set; }

    private TrackingHandler Handler { get; } = new();

    private bool KeepRiftAlive { get; set; }
    private bool ReduceResolution { get; set; }
    private int PredictionMs { get; set; }

    private bool PluginLoaded { get; set; }
    private Page InterfaceRoot { get; set; }

    public bool IsSkeletonTracked => true;
    public bool IsPositionFilterBlockingEnabled => true;
    public bool IsPhysicsOverrideEnabled => true;
    public bool IsSelfUpdateEnabled => false;
    public bool IsFlipSupported => false;
    public bool IsAppOrientationSupported => false;

    public bool IsSettingsDaemonSupported =>
        Handler.StatusResult == (int)HandlerStatus.ServiceSuccess;

    public object SettingsInterfaceRoot => InterfaceRoot;

    public ObservableCollection<TrackedJoint> TrackedJoints { get; } = new()
    {
        new TrackedJoint { Name = "INVALID", Role = TrackedJointType.JointManual }
    };

    public int DeviceStatus => Handler.StatusResult;

    public Uri ErrorDocsUri => null;

    public bool IsInitialized => Handler.IsInitialized;

    public string DeviceStatusString => PluginLoaded
        ? DeviceStatus switch
        {
            (int)HandlerStatus.ServiceNotStarted => Host.RequestLocalizedString(
                "/Plugins/TouchLink/Statuses/NotStarted"),
            (int)HandlerStatus.ServiceSuccess => Host.RequestLocalizedString("/Plugins/TouchLink/Statuses/Success"),
            (int)HandlerStatus.ErrorInitFailed =>
                Host.RequestLocalizedString("/Plugins/TouchLink/Statuses/InitFailure"),
            _ => $"Undefined: {DeviceStatus}\nE_UNDEFINED\nSomething weird has happened, though we can't tell what."
        }
        : $"Undefined: {DeviceStatus}\nE_UNDEFINED\nSomething weird has happened, though we can't tell what.";

    public void OnLoad()
    {
        // Try to load calibration values from settings
        KeepRiftAlive = Host.PluginSettings.GetSetting("KeepRiftAlive", false);
        ReduceResolution = Host.PluginSettings.GetSetting("ReduceResolution", true);
        PredictionMs = Host.PluginSettings.GetSetting("PredictionMs", 11);

        // Try to fix the recovered time offset value
        if (PredictionMs is < 0 or > 100) PredictionMs = 11;
        PredictionMs = Math.Clamp(PredictionMs, 0, 100);

        // Re-register native action handlers
        Handler.LogEvent -= LogMessageEventHandler;
        Handler.LogEvent += LogMessageEventHandler;

        // Copy the ret values to the handler
        Handler.KeepAlive = KeepRiftAlive;
        Handler.ReduceRes = ReduceResolution;
        Handler.PredictionMs = PredictionMs;

        // Settings UI setup
        PredictionTextBlock = new TextBlock
        {
            Text = Host.RequestLocalizedString("/Plugins/TouchLink/Settings/Labels/Prediction"),
            Margin = new Thickness(3),
            Opacity = 0.5
        };

        KeepAliveTextBlock = new TextBlock
        {
            Text = Host.RequestLocalizedString("/Plugins/TouchLink/Settings/Labels/KeepAlive"),
            Margin = new Thickness(3),
            Opacity = 0.5
        };

        ReduceResTextBlock = new TextBlock
        {
            Text = Host.RequestLocalizedString("/Plugins/TouchLink/Settings/Labels/ReduceRes"),
            Margin = new Thickness(3),
            Opacity = 0.5
        };

        PredictionMsNumberBox = new NumberBox
        {
            Value = PredictionMs,
            Margin = new Thickness { Left = 5 },
            SpinButtonPlacementMode = NumberBoxSpinButtonPlacementMode.Inline
        };

        KeepAliveToggleSwitch = new ToggleSwitch
        {
            IsOn = KeepRiftAlive,
            Margin = new Thickness { Left = 5, Top = -3 },
            OnContent = "", OffContent = ""
        };

        ReduceResToggleSwitch = new ToggleSwitch
        {
            IsOn = ReduceResolution,
            Margin = new Thickness { Left = 5, Top = -3 },
            OnContent = "", OffContent = ""
        };

        InterfaceRoot = new Page
        {
            Content = new StackPanel
            {
                Orientation = Orientation.Vertical,
                Children =
                {
                    new StackPanel
                    {
                        Orientation = Orientation.Horizontal,
                        Children = { PredictionTextBlock, PredictionMsNumberBox },
                        Margin = new Thickness { Bottom = 10 }
                    },
                    new StackPanel
                    {
                        Orientation = Orientation.Horizontal,
                        Children = { KeepAliveTextBlock, KeepAliveToggleSwitch }
                    },
                    new StackPanel
                    {
                        Orientation = Orientation.Horizontal,
                        Children = { ReduceResTextBlock, ReduceResToggleSwitch }
                    }
                }
            }
        };

        // Setup signals
        PredictionMsNumberBox.ValueChanged += (sender, _) =>
        {
            // Try to fix the recovered time offset value
            if (double.IsNaN(sender.Value)) sender.Value = 11;
            if (sender.Value is < 0 or > 100) sender.Value = 11;
            sender.Value = Math.Clamp(sender.Value, 0, 100);

            PredictionMs = (int)sender.Value; // Also save!
            Host.PluginSettings.SetSetting("PredictionMs", PredictionMs);
            Host.PlayAppSound(SoundType.Invoke);
        };

        KeepAliveToggleSwitch.Toggled += (sender, _) =>
        {
            KeepRiftAlive = (sender as ToggleSwitch)?.IsOn ?? false;
            Host.PluginSettings.SetSetting("KeepRiftAlive", KeepRiftAlive);
            Host.PlayAppSound(KeepRiftAlive ? SoundType.ToggleOn : SoundType.ToggleOff);
        };

        ReduceResToggleSwitch.Toggled += (sender, _) =>
        {
            ReduceResolution = (sender as ToggleSwitch)?.IsOn ?? true;
            Host.PluginSettings.SetSetting("ReduceResolution", ReduceResolution);
            Host.PlayAppSound(ReduceResolution ? SoundType.ToggleOn : SoundType.ToggleOff);
        };

        // Mark the plugin as loaded
        PluginLoaded = true;
    }

    public void Initialize()
    {
        switch (Handler.Initialize())
        {
            case (int)HandlerStatus.ServiceNotStarted:
                Host.Log(
                    $"Couldn't initialize the TouchLink device handler! Status: {HandlerStatus.ServiceNotStarted}",
                    LogSeverity.Warning);
                break;
            case (int)HandlerStatus.ServiceSuccess:
                Host.Log(
                    $"Successfully initialized the TouchLink device handler! Status: {HandlerStatus.ServiceSuccess}");
                break;
            case (int)HandlerStatus.ErrorInitFailed:
                Host.Log($"Couldn't initialize the TouchLink device handler! Status: {HandlerStatus.ErrorInitFailed}",
                    LogSeverity.Error);
                break;
        }

        Host.Log("Refreshing tracked objects vector, locking the update thread...");
        try
        {
            Host?.Log("Locking the update thread...");
            var locked = Monitor.TryEnter(Host!.UpdateThreadLock); // Try entering the lock
            Host?.Log("Emptying the tracked joints list...");
            TrackedJoints.Clear(); // Delete literally everything

            Host?.Log("Searching for tracked objects...");

            var objects = Handler.TrackedJoints;
            if (objects is null)
            {
                Host?.Log("Didn't find any valid objects within the TouchLink Service!");
                TrackedJoints.Add(new TrackedJoint
                {
                    Name = "INVALID", Role = TrackedJointType.JointManual
                });
                goto refresh; // Refresh everything after the change
            }

            // Add all the available objects
            foreach (var vrObject in objects)
            {
                Host?.Log($"Found a valid, usable tracked object: {vrObject}");
                Host?.Log("Adding the new tracked object to the controller list...");
                TrackedJoints.Add(new TrackedJoint
                {
                    Name = vrObject.Name, Role = TrackedJointType.JointManual
                });
            }

            // Exit the lock if it was us who acquired it
            if (locked) Monitor.Exit(Host!.UpdateThreadLock);

            // Jump straight to the function end
            refresh:

            // Refresh everything after the change
            Host?.Log("Refreshing the UI...");
            Host?.RefreshStatusInterface();
        }
        catch (Exception e)
        {
            Host?.Log($"Couldn't connect to the TouchLink Service! {e.Message}");
        }
    }

    public void Shutdown()
    {
        switch (Handler.Shutdown())
        {
            case 0:
                Host.Log($"Tried to shutdown the TouchLink device handler with status: {Handler.StatusResult}");
                break;
            default:
                Host.Log("Tried to shutdown the TouchLink device handler, exception occurred!", LogSeverity.Error);
                break;
        }
    }

    public void Update()
    {
        // That's all if the server is failing!
        if (!PluginLoaded || !Handler.IsInitialized ||
            Handler.StatusResult != (int)HandlerStatus.ServiceSuccess) return;

        // Try connecting to the service
        try
        {
            Handler.Update(); // Update the service

            // Refresh all controllers/all
            using var jointEnumerator = TrackedJoints.GetEnumerator();
            Handler.TrackedJoints?.ToList()
                .ForEach(vrObject =>
                {
                    // Ove to the next controller list entry
                    if (!jointEnumerator.MoveNext() ||
                        jointEnumerator.Current is null) return;

                    // Copy pose data from the controller
                    jointEnumerator.Current.Position = vrObject.Position.ToNet();
                    jointEnumerator.Current.Orientation = vrObject.Orientation.ToNet();

                    // Copy physics data from the controller
                    jointEnumerator.Current.Velocity = vrObject.Velocity.ToNet();
                    jointEnumerator.Current.Acceleration = vrObject.Acceleration.ToNet();
                    jointEnumerator.Current.AngularVelocity = vrObject.AngularVelocity.ToNet();
                    jointEnumerator.Current.AngularAcceleration = vrObject.AngularAcceleration.ToNet();

                    // Parse/copy the tracking state
                    jointEnumerator.Current.TrackingState = TrackedJointState.StateTracked;
                });
        }
        catch (Exception e)
        {
            Host?.Log($"Couldn't update TouchLink Service! {e.Message}");
        }
    }

    public void SignalJoint(int jointId)
    {
        // ignored
    }

    private void LogMessageEventHandler(object sender, string message)
    {
        // Compute severity
        var severity = message.Length >= 2
            ? int.TryParse(message[1].ToString(), out var parsed) ? Math.Clamp(parsed, 0, 3) : 0
            : 0; // Default to LogSeverity.Info

        // Log a message to AME
        Host?.Log(message, (LogSeverity)severity);
    }

    private enum HandlerStatus
    {
        ServiceNotStarted = 0x00010002, // Not initialized
        ServiceSuccess = 0, // Success, everything's fine!
        ErrorInitFailed = 0x00010001 // Init failed
    }

    #region UI Elements

    private TextBlock PredictionTextBlock { get; set; }
    private TextBlock KeepAliveTextBlock { get; set; }
    private TextBlock ReduceResTextBlock { get; set; }

    private ToggleSwitch KeepAliveToggleSwitch { get; set; }
    private ToggleSwitch ReduceResToggleSwitch { get; set; }

    private NumberBox PredictionMsNumberBox { get; set; }

    #endregion
}

internal static class ProjectionExtensions
{
    public static Vector3 ToNet(this DVector vector)
    {
        return new Vector3(vector.X, vector.Y, vector.Z);
    }

    public static Quaternion ToNet(this DQuaternion quaternion)
    {
        return new Quaternion(quaternion.X, quaternion.Y, quaternion.Z, quaternion.W);
    }
}