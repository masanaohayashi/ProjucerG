/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   JUCE is an open source framework subject to commercial or open source
   licensing.

   By downloading, installing, or using the JUCE framework, or combining the
   JUCE framework with any other source code, object code, content or any other
   copyrightable work, you agree to the terms of the JUCE End User Licence
   Agreement, and all incorporated terms including the JUCE Privacy Policy and
   the JUCE Website Terms of Service, as applicable, which will bind you. If you
   do not agree to the terms of these agreements, we will not license the JUCE
   framework to you, and you must discontinue the installation or download
   process and cease use of the JUCE framework.

   JUCE End User Licence Agreement: https://juce.com/legal/juce-8-licence/
   JUCE Privacy Policy: https://juce.com/juce-privacy-policy
   JUCE Website Terms of Service: https://juce.com/juce-website-terms-of-service/

   Or:

   You may also use this code under the terms of the AGPLv3:
   https://www.gnu.org/licenses/agpl-3.0.en.html

   THE JUCE FRAMEWORK IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL
   WARRANTIES, WHETHER EXPRESSED OR IMPLIED, INCLUDING WARRANTY OF
   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, ARE DISCLAIMED.

  ==============================================================================
*/

#pragma once

#include "../../../OnDeviceBuild/include/OnDeviceBuild/Language.h"

#if JUCE_IOS
#include "../OnDevice/jucer_OnDeviceBuildController.h"
#endif

//==============================================================================
inline void writeOnDeviceManifest (const ProjectExporter& constExporter)
{
    if (! constExporter.isiOS())
        return;

    auto& exporter = const_cast<ProjectExporter&> (constExporter);

    if (! exporter.getTargetFolder().createDirectory())
        throw build_tools::SaveError ("Can't create folder: " + exporter.getTargetFolder().getFullPathName());

    auto* root = new DynamicObject();
    root->setProperty ("name", exporter.getProject().getProjectNameString());
    root->setProperty ("bundleId", exporter.getProject().getBundleIdentifierString());
    root->setProperty ("minimumOSVersion", "17.0");

    auto defs = mergePreprocessorDefs (exporter.getProject().getAppConfigDefs(), exporter.getAllPreprocessorDefs());
    defs.set ("JUCE_IOS", "1");
    defs.set ("JUCE_IPHONE", "1");
    defs.set ("JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED", "1");
    Array<var> defineList;

    for (int i = 0; i < defs.size(); ++i)
        defineList.add (defs.getAllKeys()[i] + "=" + defs.getAllValues()[i]);

    root->setProperty ("defines", var (defineList));

    StringArray includes;
    includes.addIfNotAlreadyThere (exporter.getProject().getRelativePathForFile (exporter.getProject().getGeneratedCodeFolder())
                                       .replaceCharacter ('\\', '/'));

    const auto addHeaderPath = [&exporter, &includes] (const String& path)
    {
        auto unix = path.replaceCharacter ('\\', '/').trim();

        if (unix.isEmpty())
            return;

        if (unix == ".")
        {
            includes.addIfNotAlreadyThere (".");
            return;
        }

        if (File::isAbsolutePath (unix))
        {
            const auto relative = exporter.getProject().getRelativePathForFile (File (unix))
                                      .replaceCharacter ('\\', '/');
            includes.addIfNotAlreadyThere (relative.isNotEmpty() ? relative : unix);
            return;
        }

        const build_tools::RelativePath buildPath (unix, build_tools::RelativePath::buildTargetFolder);
        includes.addIfNotAlreadyThere (exporter.rebaseFromBuildTargetToProjectFolder (buildPath).toUnixStyle());
    };

    for (const auto& path : exporter.extraSearchPaths)
        addHeaderPath (path);

    /*  Xcode と同じ。jucer の headerPath（プロジェクトと Configuration）を
        -I に載せる。TWV の Source と Teensy4 のような隣同士のフォルダは
        ここに書いてある。 */
    if (exporter.getNumConfigurations() > 0)
        if (auto config = exporter.getConfiguration (0))
            for (const auto& path : config->getHeaderSearchPaths())
                addHeaderPath (path);

    Array<var> includeList;

    for (const auto& include : includes)
        includeList.add (include);

    root->setProperty ("includes", var (includeList));

    const std::function<void (const Project::Item&, Array<var>&)> collect =
        [&] (const Project::Item& item, Array<var>& sourceList)
    {
        if (item.isGroup())
        {
            for (int i = 0; i < item.getNumChildren(); ++i)
                collect (item.getChild (i), sourceList);

            return;
        }

        if (! (item.shouldBeAddedToTargetExporter (exporter) && item.shouldBeCompiled()))
            return;

        const auto file = item.getFile();

        if (! exporter.shouldFileBeCompiledByDefault (file))
            return;

        // Xcode compiles JuceLibraryCode/include_juce_*.mm wrappers, not the
        // module unity files sitting in the JUCE tree. Those wrappers pull in
        // juce_events.cpp; compiling the raw .mm instead can emit references
        // without the implementations.
        const auto generatedFolder = exporter.getProject().getGeneratedCodeFolder();

        if (item.isModuleCode() && ! file.isAChildOf (generatedFolder))
            return;

        const auto relative = exporter.getProject().getRelativePathForFile (file).replaceCharacter ('\\', '/');
        const bool compileAsObjC = item.isModuleCode() || file.hasFileExtension ("mm;m");

        auto* source = new DynamicObject();
        source->setProperty ("file", relative);
        source->setProperty ("language", String (ondevice::languageForSource (relative.toStdString(), compileAsObjC)));
        sourceList.add (var (source));
    };

    Array<var> sourceList;

    for (const auto& group : exporter.getAllGroups())
        collect (group, sourceList);

    root->setProperty ("sources", var (sourceList));

    StringArray frameworks;

    if (auto* list = exporter.getiOSFrameworksList())
        frameworks = *list;

    // Xcode keeps weak frameworks in a separate build phase. The in-process
    // linker used by OnDevice builds has no weak-framework phase, so include
    // those SDK frameworks in the manifest as normal links as well.
    if (auto* list = exporter.getiOSWeakFrameworksList())
        frameworks.addArray (*list);

    frameworks.addIfNotAlreadyThere ("UIKit");
    frameworks.addIfNotAlreadyThere ("Foundation");
    frameworks.trim();
    frameworks.removeDuplicates (false);
    frameworks.removeEmptyStrings();

    Array<var> frameworkList;

    for (const auto& framework : frameworks)
        frameworkList.add (framework);

    root->setProperty ("frameworks", var (frameworkList));

    StringArray libraries { "System", "c++" };

    if (auto* list = exporter.getiOSLibsList())
        libraries.addArray (*list);

    libraries.trim();
    libraries.removeDuplicates (false);
    libraries.removeEmptyStrings();

    Array<var> libraryList;

    for (const auto& library : libraries)
        libraryList.add (library);

    root->setProperty ("libraries", var (libraryList));

    build_tools::writeStreamToFile (exporter.getTargetFolder().getChildFile ("manifest.json"), [&] (MemoryOutputStream& mo)
    {
        mo.setNewLineString (exporter.getNewLineString());
        mo << JSON::toString (var (root), false);
    });
}

//==============================================================================
class OnDeviceProjectExporter final : public ProjectExporter
{
protected:
    class OnDeviceBuildConfiguration final : public BuildConfiguration
    {
    public:
        OnDeviceBuildConfiguration (Project& p, const ValueTree& settingsToUse, const ProjectExporter& e)
            : BuildConfiguration (p, settingsToUse, e)
        {
        }

        void createConfigProperties (PropertyListBuilder&) override {}
        String getModuleLibraryArchName() const override { return "arm64"; }
    };

    BuildConfiguration::Ptr createBuildConfig (const ValueTree& tree) const override
    {
        return *new OnDeviceBuildConfiguration (project, tree, *this);
    }

public:
    static String getDisplayName()        { return "On-Device"; }
    static String getValueTreeTypeName()  { return "ONDEVICE_IOS"; }
    static String getTargetFolderName()   { return "OnDevice"; }

    Identifier getExporterIdentifier() const override { return getValueTreeTypeName(); }

    static OnDeviceProjectExporter* createForSettings (Project& projectToUse, const ValueTree& settingsToUse)
    {
        if (settingsToUse.hasType (getValueTreeTypeName()))
            return new OnDeviceProjectExporter (projectToUse, settingsToUse);

        return nullptr;
    }

    OnDeviceProjectExporter (Project& p, const ValueTree& t)
        : ProjectExporter (p, t)
    {
        name = getDisplayName();
        targetLocationValue.setDefault (getDefaultBuildsRootFolder() + getTargetFolderName());
    }

    bool canLaunchProject() override
    {
       #if JUCE_IOS
        return true;
       #else
        return false;
       #endif
    }

    bool launchProject() override
    {
       #if JUCE_IOS
        return startOnDeviceBuild (*this);
       #else
        return false;
       #endif
    }

    bool usesMMFiles() const override                       { return true; }
    bool canCopeWithDuplicateFiles() override               { return true; }
    bool supportsUserDefinedConfigurations() const override { return true; }

    bool isXcode() const override                           { return false; }
    bool isVisualStudio() const override                    { return false; }
    bool isMakefile() const override                        { return false; }
    bool isAndroidStudio() const override                   { return false; }

    bool isAndroid() const override                         { return false; }
    bool isWindows() const override                         { return false; }
    bool isLinux() const override                           { return false; }
    bool isOSX() const override                             { return false; }
    bool isiOS() const override                             { return true; }

    StringArray* getiOSFrameworksList() override            { return &iosFrameworks; }
    StringArray* getiOSLibsList() override                  { return &iosLibs; }

    String getNewLineString() const override                { return "\n"; }

    bool shouldFileBeCompiledByDefault (const File& file) const override
    {
        return file.hasFileExtension ("cpp;cc;cxx;c;mm;m");
    }

    bool supportsTargetType (build_tools::ProjectType::Target::Type type) const override
    {
        using Target = build_tools::ProjectType::Target;

        return type == Target::GUIApp || type == Target::ConsoleApp;
    }

    void createExporterProperties (PropertyListBuilder&) override {}

    void addPlatformSpecificSettingsForProjectType (const build_tools::ProjectType&) override {}

    mutable StringArray iosFrameworks, iosLibs;

    void create (const OwnedArray<LibraryModule>&) const override
    {
        writeOnDeviceManifest (*this);
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OnDeviceProjectExporter)
};
