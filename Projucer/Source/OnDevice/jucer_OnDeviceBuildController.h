/*
  ==============================================================================

   This file is part of the JUCE framework.

  ==============================================================================
*/

#pragma once

#include <atomic>
#include <juce_core/juce_core.h>

class ProjectExporter;

bool startOnDeviceBuild (ProjectExporter&);

#if JUCE_IOS
/*  ビルドボタンと同じ。プロジェクトを保存してマニフェストを書き、
    On-Device Build を走らせログを logOut に積む。成功なら true。 */
bool runOnDeviceBuildCapturingLog (const juce::File& projectRoot,
                                   juce::String& logOut,
                                   std::atomic<bool>& cancelled);
#endif
