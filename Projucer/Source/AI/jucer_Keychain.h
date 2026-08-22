#pragma once

#include <juce_core/juce_core.h>

/*  Read and write Keychain items. The same implementation is used on macOS and iOS.

    Refresh tokens are long-lived credentials, so do not store them in plain-text files.
    Use kSecAttrAccessibleAfterFirstUnlockThisDeviceOnly for accessibility.
    This prevents synchronization outside the device while allowing access in the
    background after the first unlock.
*/

/** Store a value, replacing any existing value. Returns true on success. */
bool keychainWrite (const juce::String& service, const juce::String& account, const juce::String& value);

/** Read a value. Returns an empty string if it does not exist. */
juce::String keychainRead (const juce::String& service, const juce::String& account);

/** Delete a value. Missing values are not treated as errors. */
void keychainErase (const juce::String& service, const juce::String& account);
