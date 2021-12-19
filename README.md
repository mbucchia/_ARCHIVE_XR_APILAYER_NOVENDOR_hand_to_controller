# OpenXR API Layer for adapting hand tracking to controller inputs

This software enables the of hand tracking devices (such as the [Leap Motion Controller](https://www.ultraleap.com/product/leap-motion-controller/)) with applications that do not natively support hand tracking but instead support VR controller input.

The software is an OpenXR API layer that sits between the application and another OpenXR API layer (such as the [Ultraleap OpenXR Hand Tracking API Layer](https://github.com/ultraleap/OpenXRHandTracking)) or the OpenXR runtime itself, and translates the `XR_EXT_hand_tracking` API calls into standard `XrSpace` and `XrAction` behavior.

DISCLAIMER: This software is distributed as-is, without any warranties or conditions of any kind. Use at your own risks.

## Installation

Please visit the release page to download the installer for the latest version: https://github.com/mbucchia/XR_APILAYER_NOVENDOR_hand_to_controller/releases.

Once installed, use the _OpenXR hand to controller configuration tool_ to confirm that the software is active and to configure it.

## Using with Leap Motion

1. Download and install the [Leap Motion tracking software](https://developer.leapmotion.com/tracking-software-download).

2. Use the included Visualizer app to confirm that the Leap Motion Controller is properly setup and functional.

3. Download and install the [Ultraleap OpenXR Hand Tracking API Layer](https://github.com/ultraleap/OpenXRHandTracking).

## Configuration

### Determine the OpenXR application name

Each application registers itself with a name. This name is set by the application developer and is likely different from the "well-known" name of the application.

1. Tun the application you wish to enable hand tracking for. In this example, we start Microsoft Flight Simulator 2020.

2. Locate the log file for the software. It will typically be stored at `%LocalAppData%\XR_APILAYER_NOVENDOR_hand_to_controller.log`.

3. In the log file, search for the first line reading "Could not load config for ...". The name specified on this line is the application name:

```
dllHome is "C:\Program Files\OpenXR-API-Layers"
XR_APILAYER_NOVENDOR_hand_to_controller layer is active
Could not load config for "FS2020"
```

### Creating a configuration file

The configuration is stored in a file named after the OpenXR application name (determined above), with the `.cfg` extension. This file must be created where the hand to controller software is installed (ie: along side `XR_APILAYER_NOVENDOR_hand_to_controller.dll`).

By default, an empty configuration will emulate an HP Mixed Reality motion controller with basic bindings:

- Pinching (index to thumb) acts as the controller's trigger;
- Squeezing (middle, ring and little fingers) acts as the controller's squeezing;
- Tapping the wrist of the left hand with the index of the right hand acts as the controller's menu button;
- Tapping the tip of the index of the right hand with the index of the left hand acts as the controller's B button;

Use the _OpenXR hand to controller configuration tool_ to adjust configuration, and use the Save feature to write to the configuration file, using the OpenXR application name.

## Known issues and limitations

* Binding multiple gestures to the same action is presently broken.
* Hand display is only supported with DirectX 11 applications.
* Hand display opacity is not implemented (always 100% opaque).
* The software was only tested with the Windows Mixed Reality OpenXR runtime.
* The software is not optimized.

## Contributions

The author is Matthieu Bucchianeri (https://github.com/mbucchia/). Please note that this software is not affiliated with Microsoft nor Ultraleap.

Special thanks to Roman Bershadsky (https://flightsimulation.romandesign.ca/) for his ideas on how this software should work!

Special thanks to the beta testers:
- Roman Bershadsky
- RCFlyer51
- Booms3220
