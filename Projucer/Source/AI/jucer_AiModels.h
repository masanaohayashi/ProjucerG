#pragma once

#include <juce_core/juce_core.h>

/*  選べるモデルと推論の強さ（reasoning effort）、およびその保存。

    一覧と既定値は Codex が app-server の `model/list` で返す内容に合わせてある。
    Codex の /model と同じく、モデルを選ぶと続けて effort を選ぶ二段構成にする。

    サービス側でモデルが増減しても、ここを直すまではアプリに現れない。実際の
    一覧を取る HTTP エンドポイントが分かった時点で、ここを差し替える。
*/
namespace AiModels
{
    struct Effort
    {
        const char* id;            ///< リクエストに載せる値
        const char* label;         ///< 表示名
        const char* description;
    };

    enum class Provider { chatgpt, grok };

    struct Model
    {
        const char* id;
        const char* displayName;
        const char* description;
        const char* defaultEffort;
        Provider provider = Provider::chatgpt;
    };

    /** 既知のモデル。先頭がサービス側の既定。 */
    juce::Array<Model> getKnownModels();

    Provider providerFor (const juce::String& modelId);
    Provider getSelectedProvider();

    /** そのモデルで選べる effort。 */
    juce::Array<Effort> getEffortsFor (const juce::String& modelId);

    juce::String getDefaultModel();
    juce::String getDefaultModelFor (Provider provider);

    /** 現在の選択。未設定なら既定。 */
    juce::String getSelectedModel();
    juce::String getSelectedEffort();

    /*  速度。Codex の serviceTiers に対応する。既定（標準）は空文字列で、
        その場合リクエストに service_tier を載せない。 */
    struct SpeedTier
    {
        const char* id;            ///< 空文字列なら標準（送らない）
        const char* label;
        const char* description;
    };

    juce::Array<SpeedTier> getSpeedTiersFor (const juce::String& modelId);
    juce::String getSelectedSpeedTier();
    void setSelectedSpeedTier (const juce::String& tierId);
    juce::String labelForSpeedTier (const juce::String& tierId);

    /** 選択を保存する。effort が空なら、そのモデルの既定を採る。 */
    void setSelection (const juce::String& modelId, const juce::String& effortId);

    /** 表示用。例: "GPT-5.6-Sol (medium)" */
    juce::String describeSelection();

    /** 表示名。未知の ID はそのまま返す。 */
    juce::String displayNameFor (const juce::String& modelId);
    juce::String labelForEffort (const juce::String& effortId);
}
