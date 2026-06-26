#pragma once

// Windows
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

// DirectX
#include <DirectXMath.h>
#include <DirectXCollision.h>
#include <d3d11.h>
#include <wrl/client.h>

// STL
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <functional>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>

// RTTR
#include <rttr/type.h>
#include <rttr/registration.h>
#include <rttr/instance.h>
#include <rttr/variant.h>

// nlohmann JSON
#include "ThirdParty/json/json.hpp"

// Engine foundation
#include "Runtime/Foundation/Logger.h"
