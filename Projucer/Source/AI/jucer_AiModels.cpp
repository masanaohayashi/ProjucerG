#include "../Application/jucer_Headers.h"

#include "jucer_AiModels.h"

namespace AiModels
{
    namespace
    {
        constexpr const char* modelKey  = "aiModel";
        constexpr const char* effortKey = "aiModelReasoningEffort";
        constexpr const char* speedKey  = "aiModelServiceTier";
    }

    juce::Array<Model> getKnownModels()
    {
        /*  Codex の `model/list` が返す並び（hidden を除く）。先頭が既定。
            defaultEffort も同じ応答の defaultReasoningEffort に合わせてある。 */
        juce::Array<Model> models;

        models.add ({ "gpt-5.6-sol",   "GPT-5.6-Sol",   "Latest frontier agentic coding model.",                            "low" });
        models.add ({ "gpt-5.6-terra", "GPT-5.6-Terra", "Balanced agentic coding model for everyday work.",                 "medium" });
        models.add ({ "gpt-5.6-luna",  "GPT-5.6-Luna",  "Fast and affordable agentic coding model.",                        "medium" });
        models.add ({ "gpt-5.5",       "GPT-5.5",       "Frontier model for complex coding, research, and real-world work.", "medium" });
        models.add ({ "gpt-5.4",       "GPT-5.4",       "Strong model for everyday coding.",                                "medium" });
        models.add ({ "gpt-5.4-mini",  "GPT-5.4-Mini",  "Small, fast, and cost-efficient model for simpler coding tasks.",  "medium" });

        return models;
    }

    juce::Array<Effort> getEffortsFor (const juce::String&)
    {
        /*  現状、一覧のどのモデルも同じ 6 段階を返す。モデルごとに差が出たら
            引数で分岐させる。 */
        juce::Array<Effort> efforts;

        efforts.add ({ "low",    "Low",        "Fast responses with lighter reasoning" });
        efforts.add ({ "medium", "Medium",     "Balances speed and reasoning depth for everyday tasks" });
        efforts.add ({ "high",   "High",       "Greater reasoning depth for complex problems" });
        efforts.add ({ "xhigh",  "Extra high", "Extra high reasoning depth for complex problems" });
        efforts.add ({ "max",    "Max",        "Maximum reasoning depth for the hardest problems" });
        efforts.add ({ "ultra",  "Ultra",      "Maximum reasoning with automatic task delegation" });

        return efforts;
    }

    juce::Array<SpeedTier> getSpeedTiersFor (const juce::String& modelId)
    {
        juce::Array<SpeedTier> tiers;

        // 標準は常に選べる。service_tier を送らない状態。
        tiers.add ({ "", "Standard", "Standard speed" });

        /*  Codex の model/list が返す serviceTiers。現状はどのモデルも
            priority (Fast) のみで、gpt-5.2 のような古いモデルには無い。 */
        for (const auto& model : getKnownModels())
            if (modelId == model.id)
                tiers.add ({ "priority", "Fast", "1.5x speed, increased usage" });

        return tiers;
    }

    juce::String getSelectedSpeedTier()
    {
        return getGlobalProperties().getValue (speedKey, {});
    }

    void setSelectedSpeedTier (const juce::String& tierId)
    {
        auto& properties = getGlobalProperties();

        if (tierId.isEmpty())
            properties.removeValue (speedKey);
        else
            properties.setValue (speedKey, tierId);
    }

    juce::String labelForSpeedTier (const juce::String& tierId)
    {
        for (const auto& tier : getSpeedTiersFor (getSelectedModel()))
            if (tierId == tier.id)
                return tier.label;

        return tierId.isEmpty() ? "Standard" : tierId;
    }

    juce::String getDefaultModel()
    {
        return getKnownModels().getFirst().id;
    }

    static juce::String defaultEffortFor (const juce::String& modelId)
    {
        for (const auto& model : getKnownModels())
            if (modelId == model.id)
                return model.defaultEffort;

        return "medium";
    }

    juce::String getSelectedModel()
    {
        const auto stored = getGlobalProperties().getValue (modelKey, {});

        return stored.isNotEmpty() ? stored : getDefaultModel();
    }

    juce::String getSelectedEffort()
    {
        const auto stored = getGlobalProperties().getValue (effortKey, {});

        return stored.isNotEmpty() ? stored : defaultEffortFor (getSelectedModel());
    }

    void setSelection (const juce::String& modelId, const juce::String& effortId)
    {
        auto& properties = getGlobalProperties();

        properties.setValue (modelKey, modelId);
        properties.setValue (effortKey, effortId.isNotEmpty() ? effortId : defaultEffortFor (modelId));
    }

    juce::String displayNameFor (const juce::String& modelId)
    {
        for (const auto& model : getKnownModels())
            if (modelId == model.id)
                return model.displayName;

        return modelId;
    }

    juce::String labelForEffort (const juce::String& effortId)
    {
        for (const auto& effort : getEffortsFor ({}))
            if (effortId == effort.id)
                return effort.label;

        return effortId;
    }

    juce::String describeSelection()
    {
        auto text = displayNameFor (getSelectedModel())
                      + "  " + labelForEffort (getSelectedEffort());

        const auto tier = getSelectedSpeedTier();

        if (tier.isNotEmpty())
            text << "  " << labelForSpeedTier (tier);

        return text;
    }
}
