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

#define STRINGIFY(s) XSTRINGIFY(s)
#define XSTRINGIFY(s) #s

namespace {
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

    void Log(const char* fmt, ...);

    struct {
        bool loaded;
        std::string rawInteractionProfile;
        XrPath interactionProfile;
        bool leftHandEnabled;
        bool rightHandEnabled;

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
        DEFINE_ACTION(indexProximalTap);
        DEFINE_ACTION(littleProximalTap);

#undef DEFINE_ACTION

        void Dump()
        {
            if (loaded)
            {
                Log("Emulating interaction profile: %s\n", rawInteractionProfile.c_str());
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
                    LOG_IF_SET("index proximal", indexProximalTap);
                    LOG_IF_SET("little proximal", littleProximalTap);

#undef LOG_IF_SET
                }
            }
        }

        void Reset()
        {
            loaded = false;
            rawInteractionProfile = "/interaction_profiles/hp/mixed_reality_controller";
            interactionProfile = XR_NULL_PATH;
            leftHandEnabled = true;
            rightHandEnabled = true;
            aimJointIndex = XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT;
            gripJointIndex = XR_HAND_JOINT_PALM_EXT;
            clickThreshold = 0.75f;
            transform[0] = transform[1] = Pose::Identity();
            pinchAction[0] = pinchAction[1] = "/input/trigger/value";
            pinchNear = 0.01f;
            pinchFar = 0.06f;
            thumbPressAction[0] = thumbPressAction[1] = "";
            thumbPressNear = 0.01f;
            thumbPressFar = 0.05f;
            indexBendAction[0] = indexBendAction[1] = "";
            indexBendNear = 0.045f;
            indexBendFar = 0.07f;
            squeezeAction[0] = squeezeAction[1] = "/input/squeeze/value";
            squeezeNear = 0.01f;
            squeezeFar = 0.07f;
            palmTapAction[0] = palmTapAction[1] = "";
            palmTapNear = 0.02f;
            palmTapFar = 0.06f;
            wristTapAction[0] = wristTapAction[1] = "/input/menu/click";
            wristTapNear = 0.04f;
            wristTapFar = 0.05f;
            indexProximalTapAction[0] = "/input/y/click";
            indexProximalTapAction[1] = "/input/b/click";
            indexProximalTapNear = 0.02f;
            indexProximalTapFar = 0.035f;
            littleProximalTapAction[0] = "/input/x/click";
            littleProximalTapAction[1] = "/input/a/click";
            littleProximalTapNear = 0.02f;
            littleProximalTapFar = 0.035f;
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

                        if (name == "interactionProfile")
                        {
                            config.rawInteractionProfile = value;
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
                        else if (subName == "enabled")
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
                        else if (subName == "transform.vec")
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
                        else if (subName == "transform.quat")
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
                        else if (subName == configString)               \
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
                        PARSE_ACTION("index_proximal_tap", indexProximalTap)
                        PARSE_ACTION("little_proximal_tap", littleProximalTap)

#undef PARSE_ACTION
                    }
                }
                catch (...)
                {
                    Log("Error parsing L%u\n", lineNumber);
                }
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

            sessionId = XR_NULL_HANDLE;
        }

        DebugLog("<-- HandToController_xrDestroySession %d\n", result);

        return result;
    }

    // Utility function to translate an XrPath to a string we can use.
    std::string getPath(
        XrPath path)
    {
        // TODO: Robustness: implement proper error handling.
        char buf[XR_MAX_PATH_LENGTH];
        uint32_t count;
        xrPathToString(instanceId, path, sizeof(buf), &count, buf);
        return buf;
    }

    // Get the binding for a specific action/subaction path.
    std::string getActionFullPath(
        XrAction action,
        XrPath subactionPath)
    {
        std::string fullPath;
        const auto actionPath = actionsMap.find(action);
        if (actionPath != actionsMap.cend())
        {
            if (subactionPath != XR_NULL_PATH)
            {
                std::string subPath = getPath(subactionPath);
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

        std::string path = topLevelUserPath != XR_NULL_PATH ? getPath(topLevelUserPath) : "";
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
            const std::string interactionProfile = getPath(suggestedBindings->interactionProfile);
            Log("Application is suggesting bindings for interaction profile: %s\n", interactionProfile.c_str());

            // Look for controller bindings.
            if (interactionProfile == config.rawInteractionProfile)
            {
                for (unsigned int i = 0; i < suggestedBindings->countSuggestedBindings; i++)
                {
                    // Keep track of the XrAction for the controllers, so we can override the behavior for them.
                    // TODO: Optimization: only store grip/aim and the actions actually bound by the config file.
                    std::string fullPath = getPath(suggestedBindings->suggestedBindings[i].binding);
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
            const std::string fullPath = getActionFullPath(createInfo->action, createInfo->subactionPath);
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

    // Compute an action state based on the distance between 2 joints.
    void computeJointAction(
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
        if (!actionPath.empty() &&
            Pose::IsPoseValid(jointLocations[side1][joint1].locationFlags) &&
            Pose::IsPoseValid(jointLocations[side2][joint2].locationFlags))
        {
            const std::string path = sidePath + actionPath;

            // We ignore joints radius and assume the near/far distance are configured to account for them.
            const float distance = max(Length(jointLocations[side1][joint1].pose.position - jointLocations[side2][joint2].pose.position), 0.f);

            const float value = 1.f - (std::clamp(distance, nearDistance, farDistance) - nearDistance) / (farDistance - nearDistance);

            // TODO: Robustness: do we need to debounce actions to avoid false-triggering?

            DebugLog("Action %s -> %.3f (distance %.3f)\n", path.c_str(), value, distance);
            actionsState.insert_or_assign(path, value);

            // Create click from value for convenience (but not the other way around).
            if (path.rfind("/value") != std::string::npos)
            {
                actionsState.insert_or_assign(path.substr(0, path.length() - 6) + "/click", value);
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

                        computeJointAction(jointLocations, side, XR_HAND_JOINT_THUMB_TIP_EXT, side, XR_HAND_JOINT_INDEX_TIP_EXT, sidePath, ACTION_PARAMS(pinch));
                        computeJointAction(jointLocations, side, XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT, side, XR_HAND_JOINT_THUMB_TIP_EXT, sidePath, ACTION_PARAMS(thumbPress));
                        computeJointAction(jointLocations, side, XR_HAND_JOINT_INDEX_PROXIMAL_EXT, side, XR_HAND_JOINT_INDEX_TIP_EXT, sidePath, ACTION_PARAMS(indexBend));
                        // TODO: Feature: add squeeze (which uses 4 joints).

                        if (handResult[other_side] == XR_SUCCESS)
                        {
                            // Handle gestures made up using both hands.

                            computeJointAction(jointLocations, side, XR_HAND_JOINT_PALM_EXT, other_side, XR_HAND_JOINT_INDEX_TIP_EXT, sidePath, ACTION_PARAMS(palmTap));
                            computeJointAction(jointLocations, side, XR_HAND_JOINT_WRIST_EXT, other_side, XR_HAND_JOINT_INDEX_TIP_EXT, sidePath, ACTION_PARAMS(wristTap));
                            computeJointAction(jointLocations, side, XR_HAND_JOINT_INDEX_PROXIMAL_EXT, other_side, XR_HAND_JOINT_INDEX_TIP_EXT, sidePath, ACTION_PARAMS(indexProximalTap));
                            computeJointAction(jointLocations, side, XR_HAND_JOINT_LITTLE_PROXIMAL_EXT, other_side, XR_HAND_JOINT_INDEX_TIP_EXT, sidePath, ACTION_PARAMS(littleProximalTap));
                        }

                        // TODO: Feature: add more gesture recognition here.
#undef ACTION_PARAMS
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
        const std::string fullPath = getActionFullPath(getInfo->action, getInfo->subactionPath);
        if (!fullPath.empty())
        {
            const auto stateVar = actionsState.find(fullPath);
            if (stateVar != actionsState.cend())
            {
                auto lastState = lastBooleanChange.find(fullPath);
                const bool value = stateVar->second > config.clickThreshold;

                // TODO: Cleanliness: refactor common code with xrGetActionStateFloat() below.
                if (lastState != lastBooleanChange.end())
                {
                    const bool lastValue = lastState->second.first;
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
        const std::string fullPath = getActionFullPath(getInfo->action, getInfo->subactionPath);
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

        const std::string fullPath = getActionFullPath(getInfo->action, getInfo->subactionPath);
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
