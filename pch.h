// pch.h: This is a precompiled header file.
// Files listed below are compiled only once, improving build performance for future builds.
// This also affects IntelliSense performance, including code completion and many code browsing features.
// However, files listed here are ALL re-compiled if any one of them is updated between builds.
// Do not add files here that you will be updating frequently as this negates the performance advantage.

#ifndef PCH_H
#define PCH_H

// Standard library.
#include <cstdarg>
#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// Windows header files.
#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
#include <windows.h>
#include <unknwn.h>
#include <wrl.h>
#include <winsock2.h>
#include <ws2tcpip.h>

// D3D
#include <d3d11.h>
#include <d3dcompiler.h>

// OpenXR + Windows-specific definitions.
#define XR_USE_PLATFORM_WIN32
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

// OpenXR loader interfaces.
#include "loader_interfaces.h"

// Utilities.
#include "XrError.h"
#include "XrMath.h"
#include "XrToString.h"

#endif //PCH_H
