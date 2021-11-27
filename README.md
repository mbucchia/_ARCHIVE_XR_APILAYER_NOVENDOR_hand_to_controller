# FOV modifier OpenXR API Layer

This software enables the of hand tracking devices (such as the [Leap Motion Controller](https://www.ultraleap.com/product/leap-motion-controller/)) with applications that do not natively support hand tracking but instead support VR controller input.

The software is an OpenXR API layer that sits between the application and another OpenXR API layer (such as the [Ultraleap OpenXR Hand Tracking API Layer](https://github.com/ultraleap/OpenXRHandTracking)) or the OpenXR runtime itself, and translates the `XR_EXT_hand_tracking` API calls into standard `XrSpace` and `XrAction` behavior.

## Download

A ZIP file containing the necessary files to install and use the layer can be found on the release page: https://github.com/mbucchia/XR_APILAYER_NOVENDOR_hand_to_controller/releases.

## Setup

1. Create a folder in `%ProgramFiles%`. It's important to make it in `%ProgramFiles%` so that UWP applications can access it! For example: `C:\Program Files\OpenXR-API-Layers`.

2. Place `XR_APILAYER_NOVENDOR_hand_to_controller.json`, `XR_APILAYER_NOVENDOR_hand_to_controller.dll`, `Install-XR_APILAYER_NOVENDOR_hand_to_controller.ps1` and `Uninstall-XR_APILAYER_NOVENDOR_hand_to_controller.ps1` in the folder created above.

3. Run the script `Install-XR_APILAYER_NOVENDOR_hand_to_controller.ps1`. You will be prompted for elevation (running as Administrator).

4. Start the OpenXR Developer Tools for Windows Mixed Reality, under the *System Status* tab, scroll down to *API Layers*. A layer named `XR_APILAYER_NOVENDOR_hand_to_controller` should be listed.

## Removal

1. Go to the folder where the API layer is installed. For example: `C:\Program Files\OpenXR-API-Layers`.

2. Run the script `Uninstall-XR_APILAYER_NOVENDOR_hand_to_controller.ps1`. You will be prompted for elevation (running as Administrator).

3. Start the OpenXR Developer Tools for Windows Mixed Reality, under the *System Status* tab, scroll down to *API Layers*. There should be no layer named `XR_APILAYER_NOVENDOR_hand_to_controller`.

## App configuration

1. First, retrieve the name that the application passes to OpenXR. In order to do that, run the application while the API layer is enabled.

2. Locate the log file for the layer. It will typically be `%LocalAppData%\XR_APILAYER_NOVENDOR_hand_to_controller.log`.

3. In the log file, search for the first line saying "Could not load config for ...":

```
dllHome is "C:\Program Files\OpenXR-API-Layers"
XR_APILAYER_NOVENDOR_hand_to_controller layer is active
Could not load config for "FS2020"
Could not load config for "Zouna"
```

4. In the same folder where `XR_APILAYER_NOVENDOR_hand_to_controller.json` was copied during setup, create a file with a name matching the application name, and with the extension `.cfg`. For example `C:\Program Files\OpenXR-API-Layers\FS2020.cfg`.

The presence of the configuration file for the desired application will enable the input translation feature. Without a configuration file, even an empty one, the software will not be active.

By default, an empty configuration will emulate a Windows Mixed Reality motion controller with basic bindings:

- Pinching acts as the controller's trigger;
- Tapping the palm with the index of the opposite hand acts as the controller's menu button.

5. When running the application, the changes should take affect. Inspect the log file if it needs to be confirmed:

```
dllHome is "C:\Program Files\OpenXR-API-Layers"
XR_APILAYER_NOVENDOR_hand_to_controller layer is active
Loading config for "FS2020"
```

## Using with Leap Motion

1. Download and install the [Leap Motion tracking software](https://developer.leapmotion.com/tracking-software-download).

2. Use the included Visualizer app to confirm that the Leap Motion Controller is properly setup and functional.

3. Download and install the [Ultraleap OpenXR Hand Tracking API Layer](https://github.com/ultraleap/OpenXRHandTracking).

4. Start the OpenXR Developer Tools for Windows Mixed Reality, under the *System Status* tab, scroll down to *API Layers*. There should be a layer named `XR_APILAYER_NOVENDOR_hand_to_controller` **followed** by a layer named `XR_APILAYER_ULTRALEAP_hand_tracking`.

**Note that the order here is very important.** The `XR_APILAYER_NOVENDOR_hand_to_controller` layer must appear **before** the `XR_APILAYER_ULTRALEAP_hand_tracking` layer in order for the software to work.

If the order observed is incorrect, re-run the `Install-XR_APILAYER_NOVENDOR_hand_to_controller.ps1` script.

## Configuration file

The configuration file allows to modify the behavior of the software for each application.

TODO: Details of each option.