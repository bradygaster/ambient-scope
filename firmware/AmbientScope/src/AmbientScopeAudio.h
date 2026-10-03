#pragma once

#include "AudioAnalyzer.h"

namespace copilot {

bool beginAmbientScopeAudio();
bool ambientScopeAudioReady();
AmbientScopeSnapshot ambientScopeSnapshot();

}  // namespace copilot
