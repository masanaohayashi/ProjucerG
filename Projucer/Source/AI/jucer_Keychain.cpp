#include "jucer_Keychain.h"

#if ! defined (__APPLE__)

bool keychainWrite (const juce::String&, const juce::String&, const juce::String&)
{
    return false;
}

juce::String keychainRead (const juce::String&, const juce::String&)
{
    return {};
}

void keychainErase (const juce::String&, const juce::String&)
{
}

#endif
