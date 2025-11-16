#pragma once

#include "BaseStationDetector.h"
#include <openvr.h>
#include <vector>

class OpenVRBaseStationDetector
{
public:
    static std::vector<BaseStationInfo> DetectViaOpenVR();
};

