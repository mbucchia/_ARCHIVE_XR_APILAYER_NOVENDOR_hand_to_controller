// Copyright (c) 2021, Matthieu Bucchianeri
//
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "pch.h"

#include "HandRenderer.h"

#define STRINGIFY(s) XSTRINGIFY(s)
#define XSTRINGIFY(s) #s

namespace {
    using Microsoft::WRL::ComPtr;
    using namespace xr::math;

    // TODO: Cleanliness: refactor the code to make use of classes instead of the many hanging global variables.

    const std::string LayerName = "XR_APILAYER_NOVENDOR_hand_to_controller";

    // The path where the DLL loads config files and stores logs.
    std::string dllHome;

    // The file logger.
    std::ofstream logStream;

    // Function pointers to chain calls with the next layers and/or the OpenXR runtime.
    PFN_xrGetInstanceProcAddr next_xrGetInstanceProcAddr = nullptr;
    PFN_xrWaitFrame next_xrWaitFrame = nullptr;
    PFN_xrBeginFrame next_xrBeginFrame = nullptr;
    PFN_xrCreateSession next_xrCreateSession = nullptr;
    PFN_xrDestroySession next_xrDestroySession = nullptr;
    PFN_xrPollEvent next_xrPollEvent = nullptr;
    PFN_xrGetCurrentInteractionProfile next_xrGetCurrentInteractionProfile = nullptr;
    PFN_xrSuggestInteractionProfileBindings next_xrSuggestInteractionProfileBindings = nullptr;
    PFN_xrCreateActionSpace next_xrCreateActionSpace = nullptr;
    PFN_xrDestroySpace next_xrDestroySpace = nullptr;
    PFN_xrLocateSpace next_xrLocateSpace = nullptr;
    PFN_xrSyncActions next_xrSyncActions = nullptr;
    PFN_xrGetActionStateBoolean next_xrGetActionStateBoolean = nullptr;
    PFN_xrGetActionStateFloat next_xrGetActionStateFloat = nullptr;
    PFN_xrGetActionStatePose next_xrGetActionStatePose = nullptr;
    PFN_xrCreateSwapchain next_xrCreateSwapchain = nullptr;
    PFN_xrDestroySwapchain next_xrDestroySwapchain = nullptr;
    PFN_xrEnumerateSwapchainImages next_xrEnumerateSwapchainImages = nullptr;
    PFN_xrAcquireSwapchainImage next_xrAcquireSwapchainImage = nullptr;
    PFN_xrEndFrame next_xrEndFrame = nullptr;

    // Function pointers to interact with the runtime.
    PFN_xrCreateReferenceSpace xrCreateReferenceSpace = nullptr;
    PFN_xrPathToString xrPathToString = nullptr;
    PFN_xrStringToPath xrStringToPath = nullptr;

    // Function pointers to interact with the XR_EXT_hand_tracking extension.
    PFN_xrCreateHandTrackerEXT xrCreateHandTrackerEXT = nullptr;
    PFN_xrDestroyHandTrackerEXT xrDestroyHandTrackerEXT = nullptr;
    PFN_xrLocateHandJointsEXT xrLocateHandJointsEXT = nullptr;

    // Frame state.
    XrTime waitedFrameTime;
    XrTime begunFrameTime;

    // State of the hand tracker.
    XrInstance instanceId = XR_NULL_HANDLE;
    XrSession sessionId = XR_NULL_HANDLE;
    XrHandTrackerEXT handTracker[2]{ XR_NULL_HANDLE, XR_NULL_HANDLE };
    XrSpace referenceSpace = XR_NULL_HANDLE;

    // Mapping of XrAction and XrSpace.
    std::unordered_map<XrAction, std::vector<std::string>> actionsMap;
    std::unordered_map<XrSpace, std::pair<std::string, XrPosef>> spacesMap;

    // State of the API.
    bool needAdvertiseProfile;
    std::unordered_map<std::string, float> actionsState;
    std::unordered_map<std::string, std::pair<bool, XrTime>> lastBooleanChange;
    std::unordered_map<std::string, std::pair<float, XrTime>> lastFloatChange;

    // Hands visualization.
    ComPtr<ID3D11Device> d3d11Device = nullptr;
    HandRenderer handRenderer;
    std::unordered_map<XrSwapchain, XrSwapchainCreateInfo> swapchainInfo;
    struct SwapchainResources
    {
        ID3D11RenderTargetView* rtv;
        ID3D11DepthStencilView* dsv;
    };
    std::unordered_map<XrSwapchain, ComPtr<ID3D11Texture2D>> ownDepthBuffer;
    std::unordered_map<XrSwapchain, ComPtr<ID3D11DepthStencilView>> ownDsv;
    std::unordered_map<XrSwapchain, std::vector<SwapchainResources>> swapchainResources;
    std::unordered_map<XrSwapchain, uint32_t> swapchainIndices;


    void Log(const char* fmt, ...);

    struct {
        bool loaded;
        std::string rawInteractionProfile;
        XrPath interactionProfile;
        bool leftHandEnabled;
        bool rightHandEnabled;
        bool displayEnabled;

        // Whether to try to use the app's depth buffer or always use our own.
        bool useOwnDepthBuffer;

        // The skin tone to use for rendering the hand, 0=bright to 2=dark.
        int skinTone;

        // The opacity (alpha channel) for the hand mesh.
        float opacity;

        // Which projection layer to use for drawing the hands.
        int projLayerIndex;

        // The index of the joint (see enum XrHandJointEXT) to use for the aim pose.
        int aimJointIndex;

        // The index of the joint (see enum XrHandJointEXT) to use for the grip pose.
        int gripJointIndex;

        // The threshold (between 0 and 1) when converting a float action into a boolean action and the action is true.
        float clickThreshold;

        // The transformation to apply to the aim and grip poses.
        XrPosef transform[2];

        // The target XrAction path for a given gesture, and the near/far threshold to map the float action too (near maps to 1, far maps to 0).
#define DEFINE_ACTION(configName)           \
        std::string configName##Action[2];  \
        float configName##Near;             \
        float configName##Far;

        DEFINE_ACTION(pinch);
        DEFINE_ACTION(thumbPress);
        DEFINE_ACTION(indexBend);
        DEFINE_ACTION(squeeze);
        DEFINE_ACTION(palmTap);
        DEFINE_ACTION(wristTap);
        DEFINE_ACTION(indexTipTap);

#undef DEFINE_ACTION

        void Dump()
        {
            if (loaded)
            {
                Log("Emulating interaction profile: %s\n", rawInteractionProfile.c_str());
                if (displayEnabled)
                {
                    Log("Hands display is enabled in projection layer %d with %s depth buffer\n", projLayerIndex, useOwnDepthBuffer ? "own" : "app (if available)");
                    Log("Using %s skin tone and %.3f opacity\n", skinTone == 0 ? "bright" : skinTone == 1 ? "medium" : "dark", opacity);
                }
                if (leftHandEnabled)
                {
                    Log("Left transform: (%.3f, %.3f, %.3f) (%.3f, %.3f, %.3f, %.3f)\n",
                        transform[0].position.x, transform[0].position.y, transform[0].position.z,
                        transform[0].orientation.x, transform[0].orientation.y, transform[0].orientation.z, transform[0].orientation.w);
                }
                if (rightHandEnabled)
                {
                    Log("Right transform: (%.3f, %.3f, %.3f) (%.3f, %.3f, %.3f, %.3f)\n",
                        transform[1].position.x, transform[1].position.y, transform[1].position.z,
                        transform[1].orientation.x, transform[1].orientation.y, transform[1].orientation.z, transform[1].orientation.w);
                }
                if (leftHandEnabled || rightHandEnabled)
                {
                    Log("Grip pose uses joint: %d\n", config.gripJointIndex);
                    Log("Aim pose uses joint: %d\n", config.aimJointIndex);
                    Log("Click threshold: %.3f\n", config.clickThreshold);
                }
                for (int side = 0; side <= 1; side++)
                {
                    if ((side == 0 && !leftHandEnabled) || (side == 1 && !rightHandEnabled))
                    {
                        continue;
                    }

#define LOG_IF_SET(actionName, configName)                                                                          \
                    if (!configName##Action[side].empty())                                                          \
                    {                                                                                               \
                        Log("%s hand " #actionName " translates to: %s (near: %.3f, far: %.3f)\n", side ? "Right" : "Left",    \
                            configName##Action[side].c_str(), configName##Near, configName##Far);                   \
                    }

                    LOG_IF_SET("pinch", pinch);
                    LOG_IF_SET("thumb press", thumbPress);
                    LOG_IF_SET("index bend", indexBend);
                    LOG_IF_SET("squeeze", squeeze);
                    LOG_IF_SET("palm tap", palmTap);
                    LOG_IF_SET("wrist tap", wristTap);
                    LOG_IF_SET("index tip tap", indexTipTap);

#undef LOG_IF_SET
                }
            }
        }

        void Reset()
        {
            loaded = false;
            // NOTE: Have to maintain parity with Form1 ctor in Form1.cs.
            rawInteractionProfile = "/interaction_profiles/hp/mixed_reality_controller";
            interactionProfile = XR_NULL_PATH;
            leftHandEnabled = true;
            rightHandEnabled = true;
            displayEnabled = true;
            useOwnDepthBuffer = false;
            skinTone = 1; // Medium
            opacity = 1.0f;
            projLayerIndex = 0;
            aimJointIndex = XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT;
            gripJointIndex = XR_HAND_JOINT_PALM_EXT;
            clickThreshold = 0.75f;
            transform[0] = transform[1] = Pose::Identity();
            pinchAction[0] = pinchAction[1] = "/input/trigger/value";
            pinchNear = 0.0f;
            pinchFar = 0.05f;
            thumbPressAction[0] = thumbPressAction[1] = "";
            thumbPressNear = 0.0f;
            thumbPressFar = 0.05f;
            indexBendAction[0] = indexBendAction[1] = "";
            indexBendNear = 0.045f;
            indexBendFar = 0.07f;
            squeezeAction[0] = squeezeAction[1] = "/input/squeeze/value";
            squeezeNear = 0.035f;
            squeezeFar = 0.07f;
            palmTapAction[0] = palmTapAction[1] = "";
            palmTapNear = 0.02f;
            palmTapFar = 0.06f;
            wristTapAction[0] = "/input/menu/click";
            wristTapAction[1] = "";
            wristTapNear = 0.04f;
            wristTapFar = 0.05f;
            indexTipTapAction[0] = "";
            indexTipTapAction[1] = "/input/b/click";
            indexTipTapNear = 0.0f;
            indexTipTapFar = 0.07f;
        }
    } config;

    // Utility logging function.
    void InternalLog(
        const char* fmt,
        va_list va)
    {
        char buf[1024];
        _vsnprintf_s(buf, sizeof(buf), fmt, va);
        OutputDebugStringA(buf);
        if (logStream.is_open())
        {
            logStream << buf;
            logStream.flush();
        }
    }

    // General logging function.
    void Log(
        const char* fmt,
        ...)
    {
        va_list va;
        va_start(va, fmt);
        InternalLog(fmt, va);
        va_end(va);
    }

    // Debug logging function. Can make things very slow (only enabled on Debug builds).
    void DebugLog(
        const char* fmt,
        ...)
    {
#ifdef _DEBUG
        va_list va;
        va_start(va, fmt);
        InternalLog(fmt, va);
        va_end(va);
#endif
    }

    void ParseConfigurationStatement(
        const std::string line,
        unsigned int lineNumber = 1)
    {
        try
        {
            // TODO: Usability: handle comments, white spaces, blank lines...
            const auto offset = line.find('=');
            if (offset != std::string::npos)
            {
                const std::string name = line.substr(0, offset);
                const std::string value = line.substr(offset + 1);
                std::string subName;
                int side = -1;

                if (line.substr(0, 5) == "left.")
                {
                    side = 0;
                    subName = name.substr(5);
                }
                else if (line.substr(0, 6) == "right.")
                {
                    side = 1;
                    subName = name.substr(6);
                }

                if (name == "interaction_profile")
                {
                    config.rawInteractionProfile = value;
                }
                else if (name == "display.enabled")
                {
                    config.displayEnabled = value == "1" || value == "true";
                }
                else if (name == "force_own_depth_buffer")
                {
                    config.useOwnDepthBuffer = value == "1" || value == "true";
                }
                else if (name == "skin_tone")
                {
                    config.skinTone = std::stoi(value);
                }
                else if (name == "opacity")
                {
                    config.opacity = std::stof(value);
                }
                else if (name == "proj_layer_index")
                {
                    config.projLayerIndex = std::stoi(value);
                }
                else if (name == "aim_joint")
                {
                    config.aimJointIndex = std::stoi(value);
                }
                else if (name == "grip_joint")
                {
                    config.gripJointIndex = std::stoi(value);
                }
                else if (name == "click_threshold")
                {
                    config.clickThreshold = std::stof(value);
                }
                else if (side >= 0 && subName == "enabled")
                {
                    const bool boolValue = value == "1" || value == "true";
                    if (side == 0)
                    {
                        config.leftHandEnabled = boolValue;
                    }
                    else
                    {
                        config.rightHandEnabled = boolValue;
                    }
                }
                else if (side >= 0 && subName == "transform.vec")
                {
                    std::stringstream ss(value);
                    std::string component;
                    std::getline(ss, component, ' ');
                    config.transform[side].position.x = std::stof(component);
                    std::getline(ss, component, ' ');
                    config.transform[side].position.y = std::stof(component);
                    std::getline(ss, component, ' ');
                    config.transform[side].position.z = std::stof(component);
                }
                else if (side >= 0 && subName == "transform.quat")
                {
                    std::stringstream ss(value);
                    std::string component;
                    std::getline(ss, component, ' ');
                    config.transform[side].orientation.x = std::stof(component);
                    std::getline(ss, component, ' ');
                    config.transform[side].orientation.y = std::stof(component);
                    std::getline(ss, component, ' ');
                    config.transform[side].orientation.z = std::stof(component);
                    std::getline(ss, component, ' ');
                    config.transform[side].orientation.w = std::stof(component);
                }
#define PARSE_ACTION(configString, configName)                          \
                        else if (side >= 0 && subName == configString)   \
                        {                                               \
                            config.configName##Action[side] = value;    \
                        }                                               \
                        else if (name == configString ".near")          \
                        {                                               \
                            config.configName##Near = std::stof(value); \
                        }                                               \
                        else if (name == configString ".far")           \
                        {                                               \
                            config.configName##Far = std::stof(value);  \
                        }

                PARSE_ACTION("pinch", pinch)
                PARSE_ACTION("thumb_press", thumbPress)
                PARSE_ACTION("index_bend", indexBend)
                PARSE_ACTION("squeeze", squeeze)
                PARSE_ACTION("palm_tap", palmTap)
                PARSE_ACTION("wrist_tap", wristTap)
                PARSE_ACTION("index_tip_tap", indexTipTap)

#undef PARSE_ACTION
                else
                {
                    Log("L%u: Unrecognized option\n", lineNumber);
                }
            }
            else
            {
                Log("L%u: Improperly formatted option\n", lineNumber);
            }
        }
        catch (...)
        {
            Log("L%u: Parsing error\n", lineNumber);
        }
    }

    // Load configuration for our layer.
    bool LoadConfiguration(
        const std::string configName)
    {
        if (configName.empty())
        {
            return false;
        }

        std::ifstream configFile(std::filesystem::path(dllHome) / std::filesystem::path(configName + ".cfg"));
        if (configFile.is_open())
        {
            Log("Loading config for \"%s\"\n", configName.c_str());

            unsigned int lineNumber = 0;
            std::string line;
            while (std::getline(configFile, line))
            {
                lineNumber++;
                ParseConfigurationStatement(line, lineNumber);
            }
            configFile.close();

            config.loaded = true;

            return true;
        }

        Log("Could not load config for \"%s\"\n", configName.c_str());

        return false;
    }

    XrResult HandToController_xrWaitFrame(
        const XrSession session,
        const XrFrameWaitInfo* const frameWaitInfo,
        XrFrameState* const frameState)
    {
        DebugLog("--> HandToController_xrWaitFrame\n");

        // Call the chain to perform the actual operation.
        const XrResult result = next_xrWaitFrame(session, frameWaitInfo, frameState);
        if (result == XR_SUCCESS)
        {
            // Record the predicted display time, as we will need it to query hand poses in for xrSyncActions().
            waitedFrameTime = frameState->predictedDisplayTime;
        }

        DebugLog("<-- HandToController_xrWaitFrame %d\n", result);

        return result;
    }

    XrResult HandToController_xrBeginFrame(
        const XrSession session,
        const XrFrameBeginInfo* const frameBeginInfo)
    {
        DebugLog("--> HandToController_xrBeginFrame\n");

        // Call the chain to perform the actual operation.
        const XrResult result = next_xrBeginFrame(session, frameBeginInfo);
        if (result == XR_SUCCESS)
        {
            // Record the predicted display time, as we will need it to query hand poses in for xrSyncActions().
            begunFrameTime = waitedFrameTime;
        }

        DebugLog("<-- HandToController_xrBeginFrame %d\n", result);

        return result;
    }

    XrResult HandToController_xrCreateSession(
        const XrInstance instance,
        const XrSessionCreateInfo* const createInfo,
        XrSession* const session)
    {
        DebugLog("--> HandToController_xrCreateSession\n");

        // TODO: Compliance: for now we assume only 1 XrSession at a time. This should work for most applications.

        // Call the chain to perform the actual operation.
        const XrResult result = next_xrCreateSession(instance, createInfo, session);
        if (result == XR_SUCCESS)
        {
            // Create the hand trackers and a reference space.
            XrReferenceSpaceCreateInfo referenceSpaceCreateInfo{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
            referenceSpaceCreateInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
            referenceSpaceCreateInfo.poseInReferenceSpace = Pose::Identity();
            XrHandTrackerCreateInfoEXT leftTrackerCreateInfo{ XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT };
            leftTrackerCreateInfo.hand = XR_HAND_LEFT_EXT;
            leftTrackerCreateInfo.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;
            XrHandTrackerCreateInfoEXT rightTrackerCreateInfo{ XR_TYPE_HAND_TRACKER_CREATE_INFO_EXT };
            rightTrackerCreateInfo.hand = XR_HAND_RIGHT_EXT;
            rightTrackerCreateInfo.handJointSet = XR_HAND_JOINT_SET_DEFAULT_EXT;

            if (xrCreateReferenceSpace(*session, &referenceSpaceCreateInfo, &referenceSpace) != XR_SUCCESS ||
                xrCreateHandTrackerEXT(*session, &leftTrackerCreateInfo, &handTracker[0]) != XR_SUCCESS ||
                xrCreateHandTrackerEXT(*session, &rightTrackerCreateInfo, &handTracker[1]) != XR_SUCCESS)
            {
                Log("Failed to create hand trackers.\n");
            }
            else
            {
                sessionId = *session;
                needAdvertiseProfile = true;

                if (config.displayEnabled)
                {
                    // Get the D3D device so we can draw the hands.
                    const XrBaseInStructure* entry = reinterpret_cast<const XrBaseInStructure*>(createInfo->next);
                    while (entry)
                    {
                        if (entry->type == XR_TYPE_GRAPHICS_BINDING_D3D11_KHR)
                        {
                            // Keep track of the D3D device.
                            const XrGraphicsBindingD3D11KHR* d3dBindings = reinterpret_cast<const XrGraphicsBindingD3D11KHR*>(entry);
                            d3d11Device = d3dBindings->device;
                            handRenderer.SetDevice(d3d11Device);
                        }
                        else if (entry->type == XR_TYPE_GRAPHICS_BINDING_D3D12_KHR)
                        {
                            // TODO: Support D3D12.
                            Log("D3D12 is not supported.\n");
                        }

                        entry = entry->next;
                    }
                }
            }
        }

        DebugLog("<-- HandToController_xrCreateSession %d\n", result);

        return result;
    }

    XrResult HandToController_xrDestroySession(
        const XrSession session)
    {
        DebugLog("--> HandToController_xrDestroySession\n");

        // Call the chain to perform the actual operation.
        const XrResult result = next_xrDestroySession(session);
        if (result == XR_SUCCESS)
        {
            // Destroy the hand trackers and the reference space.
            if (referenceSpace != XR_NULL_HANDLE)
            {
                next_xrDestroySpace(referenceSpace);
                referenceSpace = XR_NULL_HANDLE;
            }
            if (handTracker[0] != XR_NULL_HANDLE)
            {
                xrDestroyHandTrackerEXT(handTracker[0]);
                handTracker[0] = XR_NULL_HANDLE;
            }
            if (handTracker[1] != XR_NULL_HANDLE)
            {
                xrDestroyHandTrackerEXT(handTracker[1]);
                handTracker[1] = XR_NULL_HANDLE;
            }

            // Destroy the graphics resources.
            ownDsv.clear();
            ownDepthBuffer.clear();
            handRenderer.SetDevice(nullptr);
            d3d11Device = nullptr;

            sessionId = XR_NULL_HANDLE;
        }

        DebugLog("<-- HandToController_xrDestroySession %d\n", result);

        return result;
    }

    // Utility function to translate an XrPath to a string we can use.
    std::string GetXrPath(
        XrPath path)
    {
        // TODO: Robustness: implement proper error handling.
        char buf[XR_MAX_PATH_LENGTH];
        uint32_t count;
        xrPathToString(instanceId, path, sizeof(buf), &count, buf);
        return buf;
    }

    // Get the binding for a specific action/subaction path.
    std::string GetXrActionFullPath(
        XrAction action,
        XrPath subactionPath)
    {
        std::string fullPath;
        const auto actionPath = actionsMap.find(action);
        if (actionPath != actionsMap.cend())
        {
            if (subactionPath != XR_NULL_PATH)
            {
                std::string subPath = GetXrPath(subactionPath);
                for (auto path : actionPath->second)
                {
                    if (path.find(subPath) == 0)
                    {
                        fullPath = path;
                        break;
                    }
                }
            }
            else
            {
                fullPath = actionPath->second[0];
            }
        }
        return fullPath;
    }

    XrResult HandToController_xrPollEvent(
        const XrInstance instance,
        XrEventDataBuffer* const eventData)
    {
        DebugLog("--> HandToController_xrPollEvent\n");

        XrResult result;

        // Advertise our interaction profile upon first call.
        if (sessionId != XR_NULL_HANDLE && needAdvertiseProfile) {
            XrEventDataInteractionProfileChanged* const buffer = reinterpret_cast<XrEventDataInteractionProfileChanged*>(eventData);
            buffer->type = XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED;
            buffer->next = nullptr;
            buffer->session = sessionId;
            needAdvertiseProfile = false;

            result = XR_SUCCESS;
        }
        else
        {
            // Call the chain to perform the actual operation.
            result = next_xrPollEvent(instance, eventData);
        }

        DebugLog("<-- HandToController_xrPollEvent %d\n", result);

        return result;
    }

    XrResult HandToController_xrGetCurrentInteractionProfile(
        const XrSession session,
        const XrPath topLevelUserPath,
        XrInteractionProfileState* const interactionProfile)
    {
        DebugLog("--> HandToController_xrGetCurrentInteractionProfile\n");

        XrResult result;

        std::string path = topLevelUserPath != XR_NULL_PATH ? GetXrPath(topLevelUserPath) : "";
        if (path.empty() || path == "/user/hand/left" || path == "/user/hand/right")
        {
            // Return our emulated interaction profile for the hands.
            interactionProfile->interactionProfile = config.interactionProfile;
            result = XR_SUCCESS;
        }
        else
        {
            // Call the chain to perform the operation for unhandled paths.
            result = next_xrGetCurrentInteractionProfile(session, topLevelUserPath, interactionProfile);
        }

        DebugLog("<-- HandToController_xrGetCurrentInteractionProfile %d\n", result);

        return result;
    }

    XrResult HandToController_xrSuggestInteractionProfileBindings(
        const XrInstance instance,
        const XrInteractionProfileSuggestedBinding* const suggestedBindings)
    {
        DebugLog("--> HandToController_xrSuggestInteractionProfileBindings\n");

        // Call the chain to perform the actual operation.
        const XrResult result = next_xrSuggestInteractionProfileBindings(instance, suggestedBindings);
        if (result == XR_SUCCESS)
        {
            const std::string interactionProfile = GetXrPath(suggestedBindings->interactionProfile);
            Log("Application is suggesting bindings for interaction profile: %s\n", interactionProfile.c_str());

            // Look for controller bindings.
            if (interactionProfile == config.rawInteractionProfile)
            {
                for (unsigned int i = 0; i < suggestedBindings->countSuggestedBindings; i++)
                {
                    // Keep track of the XrAction for the controllers, so we can override the behavior for them.
                    // TODO: Optimization: only store grip/aim and the actions actually bound by the config file.
                    std::string fullPath = GetXrPath(suggestedBindings->suggestedBindings[i].binding);
                    if (fullPath.find("/user/hand/right") == 0 || fullPath.find("/user/hand/left") == 0)
                    {
                        auto actionPath = actionsMap.find(suggestedBindings->suggestedBindings[i].action);
                        if (actionPath == actionsMap.cend())
                        {
                            actionPath = actionsMap.insert(std::make_pair(suggestedBindings->suggestedBindings[i].action, std::vector<std::string>())).first;
                        }
                        actionPath->second.push_back(fullPath);
                    }
                }

                Log("Binding to this interaction profile!\n");
            }
        }

        DebugLog("<-- HandToController_xrSuggestInteractionProfileBindings %d\n", result);

        return result;
    }

    XrResult HandToController_xrCreateActionSpace(
        const XrSession session,
        const XrActionSpaceCreateInfo* const createInfo,
        XrSpace* const space)
    {
        DebugLog("--> HandToController_xrCreateActionSpace\n");

        // Call the chain to perform the actual operation.
        const XrResult result = next_xrCreateActionSpace(session, createInfo, space);
        if (result == XR_SUCCESS)
        {
            // Keep track of the XrSpace for controllers, so we can override the behavior for them.
            // TODO: Optimization: only store grip/aim.
            const std::string fullPath = GetXrActionFullPath(createInfo->action, createInfo->subactionPath);
            if (fullPath.find("/user/hand/right") == 0 || fullPath.find("/user/hand/left") == 0)
            {
                spacesMap.insert(std::make_pair(*space, std::make_pair(fullPath, createInfo->poseInActionSpace)));
            }
        }

        DebugLog("<-- HandToController_xrCreateActionSpace %d\n", result);

        return result;
    }

    XrResult HandToController_xrDestroySpace(
        const XrSpace space)
    {
        DebugLog("--> HandToController_xrDestroySpace\n");

        // Call the chain to perform the actual operation.
        const XrResult result = next_xrDestroySpace(space);
        if (result == XR_SUCCESS)
        {
            // Update our bookkeeping.
            spacesMap.erase(space);
        }

        DebugLog("<-- HandToController_xrDestroySpace %d\n", result);

        return result;
    }

    XrResult HandToController_xrLocateSpace(
        const XrSpace space,
        const XrSpace baseSpace,
        const XrTime time,
        XrSpaceLocation* const location)
    {
        DebugLog("--> HandToController_xrLocateSpace\n");

        bool located = false;
        XrResult result;

        const auto actionSpace = spacesMap.find(space);
        if (actionSpace != spacesMap.cend())
        {
            // Override tracking behavior for the hands.
            const std::string fullPath = actionSpace->second.first;
            const XrPosef transform = actionSpace->second.second;

            const int side = fullPath.find("/user/hand/right") != std::string::npos ? 1 : 0;
            const bool isAim = fullPath.find("/input/aim/pose") != std::string::npos;
            const bool isGrip = fullPath.find("/input/grip/pose") != std::string::npos;

            if (((side == 0 && config.leftHandEnabled) || (side == 1 && config.rightHandEnabled)) && (isGrip || isAim))
            {

                DebugLog("Simulating %s controller %s\n", side ? "right" : "left", isGrip ? "grip" : "aim");

                // TODO: Compliance: need to perform validation of structs.

                XrHandJointsLocateInfoEXT locateInfo{ XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT };
                locateInfo.baseSpace = baseSpace;
                locateInfo.time = time;

                XrHandJointLocationEXT jointLocations[XR_HAND_JOINT_COUNT_EXT];
                XrHandJointLocationsEXT locations{ XR_TYPE_HAND_JOINT_LOCATIONS_EXT };
                locations.jointCount = XR_HAND_JOINT_COUNT_EXT;
                locations.jointLocations = jointLocations;

                // Translate the hand poses for the requested joint to a controller pose (XrActionSpace).
                result = xrLocateHandJointsEXT(handTracker[side], &locateInfo, &locations);
                if (result == XR_SUCCESS)
                {
                    const int joint = isGrip ? config.gripJointIndex : config.aimJointIndex;

                    location->locationFlags = locations.jointLocations[joint].locationFlags;
                    DebugLog("locationFlags %d\n", location->locationFlags);
                    location->pose = Pose::Multiply(transform, Pose::Multiply(config.transform[side], locations.jointLocations[joint].pose));
                    DebugLog("p %.3f %.3f %.3f o %.3f %.3f %.3f %.3f\n",
                        location->pose.position.x, location->pose.position.y, location->pose.position.z,
                        location->pose.orientation.x, location->pose.orientation.y, location->pose.orientation.z, location->pose.orientation.w);
                }

                located = true;
            }
        }

        if (!located)
        {
            // Call the chain to perform the operation for unhandled paths.
            result = next_xrLocateSpace(space, baseSpace, time, location);
        }

        DebugLog("<-- HandToController_xrLocateSpace %d\n", result);

        return result;
    }

    // Compute the scaled action value based on the distance between 2 joints.
    float ComputeJointActionValue(
        const XrHandJointLocationEXT jointLocations[2][XR_HAND_JOINT_COUNT_EXT],
        const int side1,
        const int joint1,
        const int side2,
        const int joint2,
        const float nearDistance,
        const float farDistance)
    {
        if (Pose::IsPoseValid(jointLocations[side1][joint1].locationFlags) &&
            Pose::IsPoseValid(jointLocations[side2][joint2].locationFlags))
        {
            // We ignore joints radius and assume the near/far distance are configured to account for them.
            const float distance = max(Length(jointLocations[side1][joint1].pose.position - jointLocations[side2][joint2].pose.position), 0.f);

            return 1.f - (std::clamp(distance, nearDistance, farDistance) - nearDistance) / (farDistance - nearDistance);
        }
        return NAN;
    }

    void RecordActionValue(
        const float value,
        const std::string& path)
    {
        // TODO: Robustness: do we need to debounce actions to avoid false-triggering?

        DebugLog("Action %s -> %.3f\n", path.c_str(), value);
        actionsState.insert_or_assign(path, value);

        // Create click from value for convenience (but not the other way around).
        if (path.rfind("/value") != std::string::npos)
        {
            actionsState.insert_or_assign(path.substr(0, path.length() - 6) + "/click", value);
        }
    }

    // Compute an action state based on the distance between 2 joints.
    void ComputeJointAction(
        const XrHandJointLocationEXT jointLocations[2][XR_HAND_JOINT_COUNT_EXT],
        const int side1,
        const int joint1,
        const int side2,
        const int joint2,
        const std::string& sidePath,
        const std::string& actionPath,
        const float nearDistance,
        const float farDistance)
    {
        if (!actionPath.empty())
        {
            const float value = ComputeJointActionValue(jointLocations, side1, joint1, side2, joint2, nearDistance, farDistance);
            if (!isnan(value))
            {
                RecordActionValue(value, sidePath + actionPath);
            }
        }
    }

    XrResult HandToController_xrSyncActions(
        const XrSession session, 
        const XrActionsSyncInfo* const syncInfo)
    {
        DebugLog("--> HandToController_xrSyncActions\n");

        // TODO: Compliance: we must handle XrActionSet.

        // Call the chain to perform the operation for all other paths.
        const XrResult result = next_xrSyncActions(session, syncInfo);
        if (result == XR_SUCCESS)
        {
            // TODO: Optimization: add caching of the hand pose between this API and xrLocateSpace() above.

            // Latch gesture state for both hands.
            // We do this regardless of whether a hand is enabled or not, in order to still handle 2-handed gestures.
            XrHandJointsLocateInfoEXT locateInfo{ XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT };
            locateInfo.baseSpace = referenceSpace;
            locateInfo.time = begunFrameTime;

            XrHandJointLocationEXT jointLocations[2][XR_HAND_JOINT_COUNT_EXT];
            XrResult handResult[2];

            for (int side = 0; side <= 1; side++)
            {
                const std::string sidePath = side ? "/user/hand/right" : "/user/hand/left";

                XrHandJointLocationsEXT locations{ XR_TYPE_HAND_JOINT_LOCATIONS_EXT };
                locations.jointCount = XR_HAND_JOINT_COUNT_EXT;
                locations.jointLocations = jointLocations[side];

                handResult[side] = xrLocateHandJointsEXT(handTracker[side], &locateInfo, &locations);
                if (handResult[side] != XR_SUCCESS)
                {
                    Log("Failed to get hand pose: %d\n", handResult);
                }

                for (int side = 0; side <= 1; side++)
                {
                    // Skip actions for disabled hands.
                    if ((side == 0 && !config.leftHandEnabled) || (side == 1 && !config.rightHandEnabled))
                    {
                        continue;
                    }

                    const std::string sidePath = side ? "/user/hand/right" : "/user/hand/left";
                    const int other_side = side ? 0 : 1;

                    if (handResult[side] == XR_SUCCESS)
                    {
                        // Handle gestures made up from one hand.

#define ACTION_PARAMS(configName) config.configName##Action[side], config.configName##Near, config.configName##Far

                        ComputeJointAction(jointLocations, side, XR_HAND_JOINT_THUMB_TIP_EXT, side, XR_HAND_JOINT_INDEX_TIP_EXT, sidePath, ACTION_PARAMS(pinch));
                        ComputeJointAction(jointLocations, side, XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT, side, XR_HAND_JOINT_THUMB_TIP_EXT, sidePath, ACTION_PARAMS(thumbPress));
                        ComputeJointAction(jointLocations, side, XR_HAND_JOINT_INDEX_PROXIMAL_EXT, side, XR_HAND_JOINT_INDEX_TIP_EXT, sidePath, ACTION_PARAMS(indexBend));

                        if (!config.squeezeAction[side].empty())
                        {
                            // Squeeze requires to look at 3 fingers.
                            float squeeze[3] = {
                                ComputeJointActionValue(jointLocations, side, XR_HAND_JOINT_MIDDLE_TIP_EXT, side, XR_HAND_JOINT_MIDDLE_METACARPAL_EXT, config.squeezeNear, config.squeezeFar),
                                ComputeJointActionValue(jointLocations, side, XR_HAND_JOINT_RING_TIP_EXT, side, XR_HAND_JOINT_RING_METACARPAL_EXT, config.squeezeNear, config.squeezeFar),
                                ComputeJointActionValue(jointLocations, side, XR_HAND_JOINT_LITTLE_TIP_EXT, side, XR_HAND_JOINT_LITTLE_METACARPAL_EXT, config.squeezeNear, config.squeezeFar)
                            };

                            // Quickly bubble sort.
                            if (squeeze[0] > squeeze[1])
                            {
                                std::swap(squeeze[0], squeeze[1]);
                            }
                            if (squeeze[0] > squeeze[2])
                            {
                                std::swap(squeeze[0], squeeze[2]);
                            }
                            if (squeeze[1] > squeeze[2])
                            {
                                std::swap(squeeze[1], squeeze[2]);
                            }

                            // Ignore the lowest value, average the other ones.
                            const float value = (squeeze[1] + squeeze[2]) / 2.f;
                            RecordActionValue(value, sidePath + config.squeezeAction[side]);
                        }

                        if (handResult[other_side] == XR_SUCCESS)
                        {
                            // Handle gestures made up using both hands.

                            ComputeJointAction(jointLocations, side, XR_HAND_JOINT_PALM_EXT, other_side, XR_HAND_JOINT_INDEX_TIP_EXT, sidePath, ACTION_PARAMS(palmTap));
                            ComputeJointAction(jointLocations, side, XR_HAND_JOINT_WRIST_EXT, other_side, XR_HAND_JOINT_INDEX_TIP_EXT, sidePath, ACTION_PARAMS(wristTap));
                            ComputeJointAction(jointLocations, side, XR_HAND_JOINT_INDEX_TIP_EXT, other_side, XR_HAND_JOINT_INDEX_TIP_EXT, sidePath, ACTION_PARAMS(indexTipTap));
                        }

                        // TODO: Feature: add more gesture recognition here.
#undef ACTION_PARAMS
                    }
                }
            }

            // Special handling for Windows key.
            for (int side = 0; side <= 1; side++)
            {
                const std::string fullPath = std::string((side == 0 ? "/user/hand/left" : "/user/hand/right")) + "/input/system/click";
                const auto stateVar = actionsState.find(fullPath);
                if (stateVar != actionsState.cend())
                {
                    auto lastState = lastBooleanChange.find(fullPath);
                    const bool value = stateVar->second >= config.clickThreshold;

                    const bool didChange = lastState != lastBooleanChange.end() && value != lastState->second.first;
                    if (lastState == lastBooleanChange.end() || didChange)
                    {
                        lastBooleanChange.insert_or_assign(fullPath, std::make_pair(value, begunFrameTime));
                    }

                    if (didChange && value)
                    {
                        INPUT input[2];
                        ZeroMemory(&input, sizeof(INPUT));
                        input[0].type = INPUT_KEYBOARD;
                        input[0].ki.wVk = VK_LWIN;
                        input[1].type = INPUT_KEYBOARD;
                        input[1].ki.wVk = VK_LWIN;
                        input[1].ki.dwFlags = KEYEVENTF_KEYUP;
                        SendInput(2, input, sizeof(INPUT));
                    }
                }
            }
        }

        DebugLog("<-- HandToController_xrSyncActions %d\n", result);

        return result;
    }

    XrResult HandToController_xrGetActionStateBoolean(
        const XrSession session,
        const XrActionStateGetInfo* const getInfo,
        XrActionStateBoolean* const state)
    {
        DebugLog("--> HandToController_xrGetActionStateBoolean\n");

        bool handled = false;
        XrResult result;

        // Translate inputs for the controllers.
        const std::string fullPath = GetXrActionFullPath(getInfo->action, getInfo->subactionPath);
        if (!fullPath.empty())
        {
            const auto stateVar = actionsState.find(fullPath);
            if (stateVar != actionsState.cend())
            {
                auto lastState = lastBooleanChange.find(fullPath);
                const bool value = stateVar->second >= config.clickThreshold;

                // TODO: Cleanliness: refactor common code with xrGetActionStateFloat() below.
                if (lastState != lastBooleanChange.end())
                {
                    const bool lastValue = lastState->second.first;
                    const XrTime lastChange = lastState->second.second;

                    // TODO: Compliance: this is technically incorrect, this value needs to be computed based on xrSyncActions() calls, not xrGetActionState*().
                    state->changedSinceLastSync = (value != lastValue) ? XR_TRUE : XR_FALSE;
                    state->lastChangeTime = (value != lastValue) ? begunFrameTime : lastChange;
                }
                else
                {
                    state->changedSinceLastSync = XR_FALSE;
                    state->lastChangeTime = begunFrameTime;
                }
                state->isActive = XR_TRUE;
                state->currentState = value ? XR_TRUE : XR_FALSE;

                lastBooleanChange.insert_or_assign(fullPath, std::make_pair(value, state->lastChangeTime));

                handled = true;
                result = XR_SUCCESS;
            }
        }

        // Call the chain to perform the operation for unhandled paths.
        if (!handled)
        {
            // TODO: Compliance: properly set isActive when not bound.
            result = next_xrGetActionStateBoolean(session, getInfo, state);
        }

        DebugLog("<-- HandToController_xrGetActionStateBoolean %d\n", result);

        return result;
    }

    XrResult HandToController_xrGetActionStateFloat(
        const XrSession session,
        const XrActionStateGetInfo* const getInfo,
        XrActionStateFloat* const state)
    {
        DebugLog("--> HandToController_xrGetActionStateFloat\n");

        bool handled = false;
        XrResult result;

        // Translate inputs for the controllers.
        const std::string fullPath = GetXrActionFullPath(getInfo->action, getInfo->subactionPath);
        if (!fullPath.empty())
        {
            const auto stateVar = actionsState.find(fullPath);
            if (stateVar != actionsState.cend())
            {
                auto lastState = lastFloatChange.find(fullPath);
                const float value = stateVar->second;

                if (lastState != lastFloatChange.end())
                {
                    const float lastValue = lastState->second.first;
                    const XrTime lastChange = lastState->second.second;

                    state->changedSinceLastSync = (value != lastValue) ? XR_TRUE : XR_FALSE;
                    state->lastChangeTime = (value != lastValue) ? begunFrameTime : lastChange;
                }
                else
                {
                    state->changedSinceLastSync = XR_FALSE;
                    state->lastChangeTime = begunFrameTime;
                }
                state->isActive = XR_TRUE;
                state->currentState = value;

                lastFloatChange.insert_or_assign(fullPath, std::make_pair(value, state->lastChangeTime));

                handled = true;
                result = XR_SUCCESS;
            }
        }

        // Call the chain to perform the operation for unhandled paths.
        if (!handled)
        {
            result = next_xrGetActionStateFloat(session, getInfo, state);
        }

        DebugLog("<-- HandToController_xrGetActionStateFloat %d\n", result);

        return result;
    }

    XrResult HandToController_xrGetActionStatePose(
        const XrSession session,
        const XrActionStateGetInfo* const getInfo,
        XrActionStatePose* const state)
    {
        DebugLog("--> HandToController_xrGetActionStatePose\n");

        XrResult result;

        const std::string fullPath = GetXrActionFullPath(getInfo->action, getInfo->subactionPath);
        if (!fullPath.empty())
        {
            // Always make the hands active.
            state->isActive = XR_TRUE;
            result = XR_SUCCESS;
        }
        else
        {
            // Call the chain to perform the operation for unhandled paths.
            result = next_xrGetActionStatePose(session, getInfo, state);
        }

        DebugLog("<-- HandToController_xrGetActionStatePose %d\n", result);

        return result;
    }

    bool IsSwapchainHandled(
        const XrSwapchain swapchain)
    {
        return swapchainInfo.find(swapchain) != swapchainInfo.cend();
    }

    XrResult HandToController_xrCreateSwapchain(
        const XrSession session,
        const XrSwapchainCreateInfo* const createInfo,
        XrSwapchain* const swapchain)
    {
        DebugLog("--> HandToController_xrCreateSwapchain\n");

        // Call the chain to perform the actual operation.
        const XrResult result = next_xrCreateSwapchain(session, createInfo, swapchain);
        if (result == XR_SUCCESS && d3d11Device)
        {
            if (createInfo->arraySize <= 2)
            {
                // We keep track of the swapchain info for when we intercept the textures in xrEnumerateSwapchainImages().
                swapchainInfo.insert_or_assign(*swapchain, *createInfo);
            }
            else
            {
                Log("Does not support swapchain with arraySize of %u\n", createInfo->arraySize);
            }
        }

        DebugLog("<-- HandToController_xrCreateSwapchain %d\n", result);

        return result;
    }

    XrResult HandToController_xrDestroySwapchain(
        const XrSwapchain swapchain)
    {
        DebugLog("--> HandToController_xrDestroySwapchain\n");

        // Call the chain to perform the actual operation.
        const XrResult result = next_xrDestroySwapchain(swapchain);
        if (result == XR_SUCCESS && IsSwapchainHandled(swapchain))
        {
            // Cleanup the resource views.
            for (auto entry : swapchainResources[swapchain])
            {
                if (entry.rtv)
                {
                    entry.rtv->Release();
                }
                if (entry.dsv)
                {
                    entry.dsv->Release();
                }
            }
            swapchainResources.erase(swapchain);
            swapchainIndices.erase(swapchain);
            swapchainInfo.erase(swapchain);
            ownDsv.erase(swapchain);
            ownDepthBuffer.erase(swapchain);
        }

        DebugLog("<-- HandToController_xrDestroySwapchain %d\n", result);

        return result;
    }

    void CreateOwnDepthBuffer(
        const XrSwapchain swapchain)
    {
        if (ownDepthBuffer.find(swapchain) != ownDepthBuffer.cend())
        {
            return;
        }

        XrSwapchainCreateInfo& imageInfo = swapchainInfo.find(swapchain)->second;

        D3D11_TEXTURE2D_DESC depthStencilDesc;
        ZeroMemory(&depthStencilDesc, sizeof(D3D11_TEXTURE2D_DESC));
        depthStencilDesc.Width = imageInfo.width;
        depthStencilDesc.Height = imageInfo.height;
        depthStencilDesc.MipLevels = 1;
        depthStencilDesc.ArraySize = imageInfo.arraySize;
        depthStencilDesc.Format = DXGI_FORMAT_D32_FLOAT;
        depthStencilDesc.SampleDesc.Count = 1;
        depthStencilDesc.Usage = D3D11_USAGE_DEFAULT;
        depthStencilDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

        ComPtr<ID3D11Texture2D> depthStencil;
        CHECK_HRCMD(d3d11Device->CreateTexture2D(&depthStencilDesc, NULL, depthStencil.GetAddressOf()));
        ownDepthBuffer.insert_or_assign(swapchain, depthStencil);

        D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc;
        ZeroMemory(&dsvDesc, sizeof(D3D11_DEPTH_STENCIL_VIEW_DESC));
        dsvDesc.Format = depthStencilDesc.Format;
        dsvDesc.ViewDimension = imageInfo.arraySize == 1 ? D3D11_DSV_DIMENSION_TEXTURE2D : D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
        dsvDesc.Texture2DArray.ArraySize = imageInfo.arraySize;

        ComPtr<ID3D11DepthStencilView> dsv;
        CHECK_HRCMD(d3d11Device->CreateDepthStencilView(depthStencil.Get(), &dsvDesc, dsv.GetAddressOf()));
        ownDsv.insert_or_assign(swapchain, dsv);
    }

    XrResult HandToController_xrEnumerateSwapchainImages(
        const XrSwapchain swapchain,
        const uint32_t imageCapacityInput,
        uint32_t* const imageCountOutput,
        XrSwapchainImageBaseHeader* const images)
    {
        DebugLog("--> HandToController_xrEnumerateSwapchainImages\n");

        // Call the chain to perform the actual operation.
        const XrResult result = next_xrEnumerateSwapchainImages(swapchain, imageCapacityInput, imageCountOutput, images);
        if (result == XR_SUCCESS && IsSwapchainHandled(swapchain) && imageCapacityInput > 0)
        {
            XrSwapchainImageD3D11KHR* d3dImages = reinterpret_cast<XrSwapchainImageD3D11KHR*>(images);
            XrSwapchainCreateInfo& imageInfo = swapchainInfo.find(swapchain)->second;
            for (uint32_t i = 0; i < *imageCountOutput; i++)
            {
                SwapchainResources resources;
                ZeroMemory(&resources, sizeof(SwapchainResources));

                // Create RTV or DSV based on the type of swapchain, so we can do some rendering!
                D3D11_RENDER_TARGET_VIEW_DESC rtvDesc;
                ZeroMemory(&rtvDesc, sizeof(D3D11_RENDER_TARGET_VIEW_DESC));
                rtvDesc.Format = (DXGI_FORMAT)imageInfo.format;
                rtvDesc.ViewDimension = imageInfo.arraySize == 1 ? D3D11_RTV_DIMENSION_TEXTURE2D : D3D11_RTV_DIMENSION_TEXTURE2DARRAY;
                rtvDesc.Texture2DArray.ArraySize = imageInfo.arraySize;

                D3D11_DEPTH_STENCIL_VIEW_DESC dsvDesc;
                ZeroMemory(&dsvDesc, sizeof(D3D11_DEPTH_STENCIL_VIEW_DESC));
                dsvDesc.Format = (DXGI_FORMAT)imageInfo.format;
                dsvDesc.ViewDimension = imageInfo.arraySize == 1 ? D3D11_DSV_DIMENSION_TEXTURE2D : D3D11_DSV_DIMENSION_TEXTURE2DARRAY;
                dsvDesc.Texture2DArray.ArraySize = imageInfo.arraySize;

                if (!(imageInfo.usageFlags & XR_SWAPCHAIN_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT))
                {
                    CHECK_HRCMD(d3d11Device->CreateRenderTargetView(d3dImages[i].texture, &rtvDesc, &resources.rtv));
                }
                else
                {
                    CHECK_HRCMD(d3d11Device->CreateDepthStencilView(d3dImages[i].texture, &dsvDesc, &resources.dsv));
                }

                swapchainResources[swapchain].push_back(resources);
            }
        }

        DebugLog("<-- HandToController_xrEnumerateSwapchainImages %d\n", result);

        return result;
    }

    XrResult HandToController_xrAcquireSwapchainImage(
        const XrSwapchain swapchain,
        const XrSwapchainImageAcquireInfo* const acquireInfo,
        uint32_t* const index)
    {
        DebugLog("--> HandToController_xrAcquireSwapchainImage\n");

        // Call the chain to perform the actual operation.
        const XrResult result = next_xrAcquireSwapchainImage(swapchain, acquireInfo, index);
        if (result == XR_SUCCESS && IsSwapchainHandled(swapchain))
        {
            // Keep track of the current texture index.
            swapchainIndices.insert_or_assign(swapchain, *index);
        }

        DebugLog("<-- HandToController_xrAcquireSwapchainImage %d\n", result);

        return result;
    }

    XrResult HandToController_xrEndFrame(
        const XrSession session,
        const XrFrameEndInfo* const frameEndInfo)
    {
        DebugLog("--> HandToController_xrEndFrame\n");

        std::vector<const XrCompositionLayerBaseHeader*> layers(frameEndInfo->layerCount);
        int projLayerIndex = 0;
        for (uint32_t i = 0; config.displayEnabled && i < frameEndInfo->layerCount; i++)
        {
            // Render the hands in the desired projection layer.
            if (frameEndInfo->layers[i]->type == XR_TYPE_COMPOSITION_LAYER_PROJECTION)
            {
                if (projLayerIndex++ != config.projLayerIndex)
                {
                    continue;
                }

                // TODO: Compliance: Lots of missing checks below.

                // Collect the info we need about this layer.
                const XrCompositionLayerProjection* proj = reinterpret_cast<const XrCompositionLayerProjection*>(frameEndInfo->layers[i]);
                const XrSwapchain colorSwapchain[2] = { proj->views[0].subImage.swapchain, proj->views[1].subImage.swapchain };
                const uint32_t colorImageArrayIndex[2] = { proj->views[0].subImage.imageArrayIndex, proj->views[1].subImage.imageArrayIndex };

                // TODO: Compliance: can't really figure out the correct logic for imageArrayIndex... For now always assume left==0 and right==0 (non-VPRT) or 1 (VPRT)

                if (!IsSwapchainHandled(colorSwapchain[0]) || !IsSwapchainHandled(colorSwapchain[1]))
                {
                    break;
                }

                // Search for the depth buffers.
                XrSwapchain depthSwapchain[2] = { XR_NULL_HANDLE, XR_NULL_HANDLE };
                float depthNear = 0.001f, depthFar = 100.0f;
                for (uint32_t j = 0; !config.useOwnDepthBuffer && j < proj->viewCount; j++)
                {
                    const auto view = proj->views[j];
                    const XrBaseInStructure* entry = reinterpret_cast<const XrBaseInStructure*>(view.next);
                    while (entry)
                    {
                        if (entry->type == XR_TYPE_COMPOSITION_LAYER_DEPTH_INFO_KHR)
                        {
                            const XrCompositionLayerDepthInfoKHR* depth = reinterpret_cast<const XrCompositionLayerDepthInfoKHR*>(entry);
                            // The order of color/depth textures must match.
                            if (depth->subImage.imageArrayIndex == colorImageArrayIndex[j])
                            {
                                depthSwapchain[j] = depth->subImage.swapchain;
                                depthNear = depth->nearZ;
                                depthFar = depth->farZ;
                            }
                            break;
                        }
                        entry = entry->next;
                    }
                }

                // Get the hand joints poses.
                // TODO: Optimization: add caching of the hand pose between this API and xrSyncActions() above.
                XrHandJointsLocateInfoEXT locateInfo{ XR_TYPE_HAND_JOINTS_LOCATE_INFO_EXT };
                locateInfo.baseSpace = proj->space;
                locateInfo.time = begunFrameTime;

                XrHandJointLocationEXT jointLocations[2][XR_HAND_JOINT_COUNT_EXT];
                XrResult handResult[2];
                for (int side = 0; side <= 1; side++)
                {
                    XrHandJointLocationsEXT locations{ XR_TYPE_HAND_JOINT_LOCATIONS_EXT };
                    locations.jointCount = XR_HAND_JOINT_COUNT_EXT;
                    locations.jointLocations = jointLocations[side];

                    handResult[side] = xrLocateHandJointsEXT(handTracker[side], &locateInfo, &locations);
                }

                // Render the hands.
                const XrSwapchain& leftColorSwapchain = colorSwapchain[0];
                const XrSwapchain& rightColorSwapchain = colorSwapchain[1];
                ID3D11RenderTargetView* const rtv[2] = {
                    swapchainResources[leftColorSwapchain][swapchainIndices[leftColorSwapchain]].rtv,
                    swapchainResources[rightColorSwapchain][swapchainIndices[rightColorSwapchain]].rtv,
                };

                const XrSwapchain& leftDepthSwapchain = depthSwapchain[0];
                const XrSwapchain& rightDepthSwapchain = depthSwapchain[1];
                const bool useOwnDepthBuffer = !IsSwapchainHandled(leftDepthSwapchain) || !IsSwapchainHandled(rightDepthSwapchain);
                if (useOwnDepthBuffer)
                {
                    CreateOwnDepthBuffer(leftColorSwapchain);
                }
                ID3D11DepthStencilView* const dsv[2] = {
                    IsSwapchainHandled(leftDepthSwapchain) ?
                        swapchainResources[leftDepthSwapchain][swapchainIndices[leftDepthSwapchain]].dsv : ownDsv[leftColorSwapchain].Get(),
                    IsSwapchainHandled(rightDepthSwapchain) ?
                        swapchainResources[rightDepthSwapchain][swapchainIndices[rightDepthSwapchain]].dsv : ownDsv[leftColorSwapchain].Get(), /* Intentionally uses the same own depth buffer for rendering */
                };

                const XrPosef eyePoses[2] = { proj->views[0].pose, proj->views[1].pose };
                const XrFovf fovs[2] = { proj->views[0].fov, proj->views[1].fov };

                const bool isVPRT = leftColorSwapchain == rightColorSwapchain;
                handRenderer.SetProperties(config.skinTone, config.opacity);
                handRenderer.SetEyePoses(eyePoses, fovs);
                handRenderer.SetJointsLocations(handResult, jointLocations);
                handRenderer.RenderHands(
                    rtv, dsv, proj->views[0].subImage.imageRect,
                    isVPRT,
                    useOwnDepthBuffer,
                    depthNear, depthFar);

                break;
            }
        }

        // Call the chain to perform the actual submission.
        const XrResult result = next_xrEndFrame(session, frameEndInfo);

        DebugLog("<-- HandToController_xrEndFrame %d\n", result);

        return result;
    }

    // Entry point for OpenXR calls.
    XrResult HandToController_xrGetInstanceProcAddr(
        const XrInstance instance,
        const char* const name,
        PFN_xrVoidFunction* const function)
    {
        DebugLog("--> HandToController_xrGetInstanceProcAddr \"%s\"\n", name);

        // Call the chain to resolve the next function pointer.
        const XrResult result = next_xrGetInstanceProcAddr(instance, name, function);
        if (config.loaded && result == XR_SUCCESS)
        {
            const std::string apiName(name);

            // Intercept the calls handled by our layer.
#define INTERCEPT_CALL(xrCall)                                                                  \
            if (apiName == STRINGIFY(xrCall)) {                                                 \
                next_##xrCall = reinterpret_cast<PFN_##xrCall>(*function);                      \
                *function = reinterpret_cast<PFN_xrVoidFunction>(HandToController_##xrCall);    \
            }

            INTERCEPT_CALL(xrWaitFrame);
            INTERCEPT_CALL(xrBeginFrame);
            INTERCEPT_CALL(xrCreateSession);
            INTERCEPT_CALL(xrDestroySession);
            INTERCEPT_CALL(xrPollEvent);
            INTERCEPT_CALL(xrGetCurrentInteractionProfile);
            INTERCEPT_CALL(xrSuggestInteractionProfileBindings);
            INTERCEPT_CALL(xrCreateActionSpace);
            INTERCEPT_CALL(xrDestroySpace);
            INTERCEPT_CALL(xrLocateSpace);
            INTERCEPT_CALL(xrSyncActions);
            INTERCEPT_CALL(xrGetActionStateBoolean);
            INTERCEPT_CALL(xrGetActionStateFloat);
            INTERCEPT_CALL(xrGetActionStatePose);
            INTERCEPT_CALL(xrCreateSwapchain);
            INTERCEPT_CALL(xrDestroySwapchain);
            INTERCEPT_CALL(xrEnumerateSwapchainImages);
            INTERCEPT_CALL(xrAcquireSwapchainImage);
            INTERCEPT_CALL(xrEndFrame);

#undef INTERCEPT_CALL

            // Leave all unhandled calls to the next layer.
        }

        DebugLog("<-- HandToController_xrGetInstanceProcAddr %d\n", result);

        return result;
    }

    // Entry point for creating the layer.
    XrResult HandToController_xrCreateApiLayerInstance(
        const XrInstanceCreateInfo* const instanceCreateInfo,
        const struct XrApiLayerCreateInfo* const apiLayerInfo,
        XrInstance* const instance)
    {
        DebugLog("--> HandToController_xrCreateApiLayerInstance\n");

        if (!apiLayerInfo ||
            apiLayerInfo->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_CREATE_INFO ||
            apiLayerInfo->structVersion != XR_API_LAYER_CREATE_INFO_STRUCT_VERSION ||
            apiLayerInfo->structSize != sizeof(XrApiLayerCreateInfo) ||
            !apiLayerInfo->nextInfo ||
            apiLayerInfo->nextInfo->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_NEXT_INFO ||
            apiLayerInfo->nextInfo->structVersion != XR_API_LAYER_NEXT_INFO_STRUCT_VERSION ||
            apiLayerInfo->nextInfo->structSize != sizeof(XrApiLayerNextInfo) ||
            apiLayerInfo->nextInfo->layerName != LayerName ||
            !apiLayerInfo->nextInfo->nextGetInstanceProcAddr ||
            !apiLayerInfo->nextInfo->nextCreateApiLayerInstance)
        {
            Log("xrCreateApiLayerInstance validation failed\n");
            return XR_ERROR_INITIALIZATION_FAILED;
        }

        // Store the next xrGetInstanceProcAddr to resolve the functions not handled by our layer.
        next_xrGetInstanceProcAddr = apiLayerInfo->nextInfo->nextGetInstanceProcAddr;

        // Check that the XR_EXT_hand_tracking extension is supported by the runtime and/or an upstream API layer.
        // TODO: Robustness: this call is illegal - the XrInstance does not exist yet... We may need to create a dummy instance in order to be able to perform these checks.
        PFN_xrEnumerateInstanceExtensionProperties next_xrEnumerateInstanceExtensionProperties = nullptr;
        next_xrGetInstanceProcAddr(*instance, "xrEnumerateInstanceExtensionProperties", reinterpret_cast<PFN_xrVoidFunction*>(&next_xrEnumerateInstanceExtensionProperties));
        // Workaround for now, seems to work with WMR runtime (and assuming there is only the Ultraleap layer behind our layer).
        if (next_xrEnumerateInstanceExtensionProperties == nullptr)
        {
            apiLayerInfo->nextInfo->next->nextGetInstanceProcAddr(*instance, "xrEnumerateInstanceExtensionProperties", reinterpret_cast<PFN_xrVoidFunction*>(&next_xrEnumerateInstanceExtensionProperties));
        }

        uint32_t extensionsCount = 0;
        next_xrEnumerateInstanceExtensionProperties(nullptr, 0, &extensionsCount, nullptr);
        std::vector<XrExtensionProperties> extensions(extensionsCount, { XR_TYPE_EXTENSION_PROPERTIES });
        next_xrEnumerateInstanceExtensionProperties(nullptr, extensionsCount, &extensionsCount, extensions.data());
        bool hasHandTrackingExt = false;
        for (auto extension : extensions) {
            const std::string extensionName(extension.extensionName);

            if (extensionName == "XR_EXT_hand_tracking")
            {
                hasHandTrackingExt = true;
            }
        }

        // Request the XR_EXT_hand_tracking extension.
        XrInstanceCreateInfo chainInstanceCreateInfo = *instanceCreateInfo;
        if (hasHandTrackingExt)
        {
            const char** newEnabledExtensionNames = new const char* [++chainInstanceCreateInfo.enabledExtensionCount];
            chainInstanceCreateInfo.enabledExtensionNames = newEnabledExtensionNames;
            memcpy(reinterpret_cast<void *>(newEnabledExtensionNames), instanceCreateInfo->enabledExtensionNames,
                instanceCreateInfo->enabledExtensionCount * sizeof(const char*));
            newEnabledExtensionNames[chainInstanceCreateInfo.enabledExtensionCount - 1] = "XR_EXT_hand_tracking";
        }
        else
        {
            Log("XR_EXT_hand_tracking is not available from the OpenXR runtime or any upsteam API layer.\n");
        }

        // Call the chain to create the instance.
        XrApiLayerCreateInfo chainApiLayerInfo = *apiLayerInfo;
        chainApiLayerInfo.nextInfo = apiLayerInfo->nextInfo->next;
        const XrResult result = apiLayerInfo->nextInfo->nextCreateApiLayerInstance(&chainInstanceCreateInfo, &chainApiLayerInfo, instance);
        if (hasHandTrackingExt)
        {
            delete[] chainInstanceCreateInfo.enabledExtensionNames;
        }

        if (result == XR_SUCCESS)
        {
            instanceId = *instance;

            config.Reset();

            actionsMap.clear();
            spacesMap.clear();
            actionsState.clear();

            // Check that the system supports hand tracking. Note that if hasHandTrackingExt is false this is a no-op.
            // TODO: Robustness: implement proper error handling.
            PFN_xrGetSystem next_xrGetSystem = nullptr;
            next_xrGetInstanceProcAddr(*instance, "xrGetSystem", reinterpret_cast<PFN_xrVoidFunction*>(&next_xrGetSystem));
            PFN_xrGetSystemProperties next_xrGetSystemProperties = nullptr;
            next_xrGetInstanceProcAddr(*instance, "xrGetSystemProperties", reinterpret_cast<PFN_xrVoidFunction*>(&next_xrGetSystemProperties));

            XrSystemGetInfo systemGetInfo{ XR_TYPE_SYSTEM_GET_INFO };
            // TODO: Compliance: we assume to always use HMD system. We should intercept the call to xrGetSystem() instead.
            systemGetInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

            XrSystemId systemId;
            next_xrGetSystem(*instance, &systemGetInfo, &systemId);

            XrSystemHandTrackingPropertiesEXT handTrackingSystemProperties{ XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT };
            XrSystemProperties systemProperties{ XR_TYPE_SYSTEM_PROPERTIES, &handTrackingSystemProperties };
            next_xrGetSystemProperties(*instance, systemId, &systemProperties);
            if (!handTrackingSystemProperties.supportsHandTracking)
            {
                Log("The XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY system does not support hand tracking.\n");
            }

            // Resolve the XR_EXT_hand_tracking symbols.
            if (!hasHandTrackingExt ||
                !handTrackingSystemProperties.supportsHandTracking ||
                next_xrGetInstanceProcAddr(*instance, "xrCreateHandTrackerEXT", reinterpret_cast<PFN_xrVoidFunction*>(&xrCreateHandTrackerEXT)) != XR_SUCCESS ||
                next_xrGetInstanceProcAddr(*instance, "xrDestroyHandTrackerEXT", reinterpret_cast<PFN_xrVoidFunction*>(&xrDestroyHandTrackerEXT)) != XR_SUCCESS ||
                next_xrGetInstanceProcAddr(*instance, "xrLocateHandJointsEXT", reinterpret_cast<PFN_xrVoidFunction*>(&xrLocateHandJointsEXT)) != XR_SUCCESS)
            {
                Log("Failed to resolve symbols for XR_EXT_hand_tracking.\n");
            }
            else
            {
                // Resolve additional symbols.
                // TODO: Robustness: implement proper error handling.
                next_xrGetInstanceProcAddr(*instance, "xrCreateReferenceSpace", reinterpret_cast<PFN_xrVoidFunction*>(&xrCreateReferenceSpace));
                next_xrGetInstanceProcAddr(*instance, "xrPathToString", reinterpret_cast<PFN_xrVoidFunction*>(&xrPathToString));
                next_xrGetInstanceProcAddr(*instance, "xrStringToPath", reinterpret_cast<PFN_xrVoidFunction*>(&xrStringToPath));

                // Identify the application and load our configuration. Try by application first, then fallback to engines otherwise.
                if (!LoadConfiguration(instanceCreateInfo->applicationInfo.applicationName)) {
                    LoadConfiguration(instanceCreateInfo->applicationInfo.engineName);
                }
                config.Dump();

                // TODO: Robustness: implement proper error handling.
                xrStringToPath(*instance, config.rawInteractionProfile.c_str(), &config.interactionProfile);
            }
        }

        DebugLog("<-- HandToController_xrCreateApiLayerInstance %d\n", result);

        return result;
    }

}

extern "C" {

    // Entry point for the loader.
    XrResult __declspec(dllexport) XRAPI_CALL HandToController_xrNegotiateLoaderApiLayerInterface(
        const XrNegotiateLoaderInfo* const loaderInfo,
        const char* const apiLayerName,
        XrNegotiateApiLayerRequest* const apiLayerRequest)
    {
        DebugLog("--> (early) HandToController_xrNegotiateLoaderApiLayerInterface\n");

        // Retrieve the path of the DLL.
        if (dllHome.empty())
        {
            HMODULE module;
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&dllHome, &module))
            {
                char path[_MAX_PATH];
                GetModuleFileNameA(module, path, sizeof(path));
                dllHome = std::filesystem::path(path).parent_path().string();
            }
            else
            {
                // Falling back to loading config/writing logs to the current working directory.
                DebugLog("Failed to locate DLL\n");
            }            
        }

        // Start logging to file.
        if (!logStream.is_open())
        {
            std::string logFile = (std::filesystem::path(getenv("LOCALAPPDATA")) / std::filesystem::path(LayerName + ".log")).string();
            logStream.open(logFile, std::ios_base::ate);
            Log("dllHome is \"%s\"\n", dllHome.c_str());
        }

        DebugLog("--> HandToController_xrNegotiateLoaderApiLayerInterface\n");

        if (apiLayerName && apiLayerName != LayerName)
        {
            Log("Invalid apiLayerName \"%s\"\n", apiLayerName);
            return XR_ERROR_INITIALIZATION_FAILED;
        }

        if (!loaderInfo ||
            !apiLayerRequest ||
            loaderInfo->structType != XR_LOADER_INTERFACE_STRUCT_LOADER_INFO ||
            loaderInfo->structVersion != XR_LOADER_INFO_STRUCT_VERSION ||
            loaderInfo->structSize != sizeof(XrNegotiateLoaderInfo) ||
            apiLayerRequest->structType != XR_LOADER_INTERFACE_STRUCT_API_LAYER_REQUEST ||
            apiLayerRequest->structVersion != XR_API_LAYER_INFO_STRUCT_VERSION ||
            apiLayerRequest->structSize != sizeof(XrNegotiateApiLayerRequest) ||
            loaderInfo->minInterfaceVersion > XR_CURRENT_LOADER_API_LAYER_VERSION ||
            loaderInfo->maxInterfaceVersion < XR_CURRENT_LOADER_API_LAYER_VERSION ||
            loaderInfo->maxInterfaceVersion > XR_CURRENT_LOADER_API_LAYER_VERSION ||
            loaderInfo->maxApiVersion < XR_CURRENT_API_VERSION ||
            loaderInfo->minApiVersion > XR_CURRENT_API_VERSION)
        {
            Log("xrNegotiateLoaderApiLayerInterface validation failed\n");
            return XR_ERROR_INITIALIZATION_FAILED;
        }

        // Setup our layer to intercept OpenXR calls.
        apiLayerRequest->layerInterfaceVersion = XR_CURRENT_LOADER_API_LAYER_VERSION;
        apiLayerRequest->layerApiVersion = XR_CURRENT_API_VERSION;
        apiLayerRequest->getInstanceProcAddr = reinterpret_cast<PFN_xrGetInstanceProcAddr>(HandToController_xrGetInstanceProcAddr);
        apiLayerRequest->createApiLayerInstance = reinterpret_cast<PFN_xrCreateApiLayerInstance>(HandToController_xrCreateApiLayerInstance);

        DebugLog("<-- HandToController_xrNegotiateLoaderApiLayerInterface\n");

        Log("%s layer is active\n", LayerName.c_str());

        return XR_SUCCESS;
    }

}
