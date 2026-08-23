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

/*  ビルド進捗パネルを開き直す。既に出ていれば前面へ。 */
void showOnDeviceBuildProgress();

/*  ビルド中か、直近のビルドのログが残っているか。 */
bool hasOnDeviceBuildProgress();
#endif
