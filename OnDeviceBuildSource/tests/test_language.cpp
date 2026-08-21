#include "OnDeviceBuild/Language.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
[[noreturn]] void fail (const char* message)
{
    std::cerr << "FAIL: " << message << '\n';
    std::exit (1);
}

void requireLanguage (const char* path, bool compileAsObjC, const char* expected)
{
    const char* actual = ondevice::languageForSource (path, compileAsObjC);

    if (std::string (actual) != expected)
    {
        std::cerr << "FAIL: " << path << " compileAsObjC=" << compileAsObjC
                  << " -> " << actual << " (expected " << expected << ")\n";
        std::exit (1);
    }
}
} // namespace

int main()
{
    requireLanguage ("Source/plain.c", false, "c");
    requireLanguage ("Source/plain.c", true, "c");
    requireLanguage ("JuceLibraryCode/include_juce_core.mm", false, "objective-c++");
    requireLanguage ("Source/Main.m", false, "objective-c++");
    requireLanguage ("Source/Main.cpp", false, "c++");
    requireLanguage ("Source/Main.cpp", true, "objective-c++");
    requireLanguage ("Source/Main.cc", false, "c++");
    requireLanguage ("foo.cpp", false, "c++");

    std::cout << "PASS\n";
    return 0;
}
