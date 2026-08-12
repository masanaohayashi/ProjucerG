/*
  ==============================================================================

   This file is part of the JUCE library.
   Copyright (c) 2022 - Raw Material Software Limited

   JUCE is an open source library subject to commercial or open-source
   licensing.

   By using JUCE, you agree to the terms of both the JUCE 7 End-User License
   Agreement and JUCE Privacy Policy.

   End User License Agreement: www.juce.com/juce-7-licence
   Privacy Policy: www.juce.com/juce-privacy-policy

   Or: You may also use this code under the terms of the GPL v3 (see
   www.gnu.org/licenses).

   JUCE IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
   EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
   DISCLAIMED.

  ==============================================================================
*/

#pragma once

// (this header is always included after jucer_Project.h / jucer_JucerDocument.h)

//==============================================================================
class FontPropertyComponent    : public ChoicePropertyComponent
{
public:
    FontPropertyComponent (const String& name, Project* projectToUse)
        : ChoicePropertyComponent (name)
    {
        choices.add (getDefaultFont());
        choices.add (getDefaultSans());
        choices.add (getDefaultSerif());
        choices.add (getDefaultMono());
        choices.add (String());

        auto projectFonts = getProjectFontNames (projectToUse);

        if (! projectFonts.isEmpty())
        {
            choices.addArray (projectFonts);
            choices.add (String());
        }

        static StringArray fontNames;

        if (fontNames.size() == 0)
        {
            Array<Font> fonts;
            Font::findFonts (fonts);

            for (int i = 0; i < fonts.size(); ++i)
                fontNames.add (fonts.getReference (i).getTypefaceName());
        }

        choices.addArray (fontNames);
    }

    static String getDefaultFont()  { return "Default font"; }
    static String getDefaultSans()  { return "Default sans-serif font"; }
    static String getDefaultSerif() { return "Default serif font"; }
    static String getDefaultMono()  { return "Default monospaced font"; }

    //==============================================================================
    /** Fonts that were added to the project as binary resources are listed by their
        file name (e.g. "MyFont.ttf"), which is also what gets stored in the document.
    */
    static bool isFontFileName (const String& name)
    {
        return name.endsWithIgnoreCase (".ttf") || name.endsWithIgnoreCase (".otf");
    }

    static StringArray getProjectFontNames (Project* project)
    {
        StringArray names;

        for (auto& f : getProjectFontFiles (project))
            names.addIfNotAlreadyThere (f.getFileName());

        return names;
    }

    /** Returns the file that a typeface name refers to, or an empty file if the name
        isn't one of the project's embedded fonts.
    */
    static File getProjectFontFile (Project* project, const String& typefaceName)
    {
        if (project != nullptr && isFontFileName (typefaceName))
            for (auto& f : getProjectFontFiles (project))
                if (f.getFileName() == typefaceName)
                    return f;

        return {};
    }

    /** Returns the BinaryData identifier that the project's resource file will use for a
        font, matching the numbering that JucerResourceFile applies to same-named files.
    */
    static String getBinaryDataIdentifier (Project* project, const File& fontFile)
    {
        StringArray used;

        for (auto& f : getProjectBinaryResources (project))
        {
            auto root = build_tools::makeBinaryDataIdentifierName (f);
            auto identifier = root;

            for (int suffix = 2; used.contains (identifier); ++suffix)
                identifier = root + String (suffix);

            used.add (identifier);

            if (f == fontFile)
                return identifier;
        }

        return {};
    }

    /** Loads (and caches) an embedded project font. Holding on to the returned pointer keeps
        the typeface registered with JUCE, so that it can be found by its family name.
    */
    static Typeface::Ptr getProjectTypeface (Project* project, const String& typefaceName)
    {
        if (project == nullptr || ! isFontFileName (typefaceName))
            return {};

        // ponytail: keyed by project + name, so a .ttf that's swapped on disk needs the
        // document reopening to show up. Add a modification-time check if that bites.
        auto& cache = getTypefaceCache();
        auto key = project->getFile().getFullPathName() + "|" + typefaceName;
        auto existing = cache.find (key);

        if (existing != cache.end())
            return existing->second;

        MemoryBlock data;

        if (! getProjectFontFile (project, typefaceName).loadFileAsData (data))
            return {};   // (not cached, so that a font that's added later will be picked up)

        auto typeface = Typeface::createSystemTypefaceFor (data.getData(), data.getSize());
        cache[key] = typeface;
        return typeface;
    }

    /** The cached typefaces must be released before JUCE shuts down. */
    static void clearTypefaceCache()
    {
        getTypefaceCache().clear();
    }

    //==============================================================================
    virtual void setTypefaceName (const String& newFontName) = 0;
    virtual String getTypefaceName() const = 0;

    //==============================================================================
    void setIndex (int newIndex) override
    {
        String type (choices [newIndex]);

        if (type.isEmpty())
            type = getDefaultFont();

        if (getTypefaceName() != type)
            setTypefaceName (type);
    }

    int getIndex() const override
    {
        return choices.indexOf (getTypefaceName());
    }

    static Font applyNameToFont (Project* project, const String& typefaceName, const Font& font)
    {
        auto extraKerning = font.getExtraKerningFactor();

        // An embedded project font is used via its family name, so that bold/italic and the
        // style list keep working exactly as they do for an installed font.
        if (auto typeface = getProjectTypeface (project, typefaceName))
            return applyNameToFont (nullptr, typeface->getName(), font);

        if (typefaceName == getDefaultFont())  return makeLegacyFont (Font::getDefaultSansSerifFontName(), font.getHeight(), font.getStyleFlags(), extraKerning);
        if (typefaceName == getDefaultSans())  return makeLegacyFont (Font::getDefaultSansSerifFontName(), font.getHeight(), font.getStyleFlags(), extraKerning);
        if (typefaceName == getDefaultSerif()) return makeLegacyFont (Font::getDefaultSerifFontName(), font.getHeight(), font.getStyleFlags(), extraKerning);
        if (typefaceName == getDefaultMono())  return makeLegacyFont (Font::getDefaultMonospacedFontName(), font.getHeight(), font.getStyleFlags(), extraKerning);

        auto f = makeLegacyFont (typefaceName, font.getHeight(), font.getStyleFlags(), extraKerning);

        if (f.getAvailableStyles().contains (font.getTypefaceStyle()))
            f.setTypefaceStyle (font.getTypefaceStyle());

        return f;
    }

    static String getTypefaceNameCode (const String& typefaceName)
    {
        if (typefaceName == getDefaultFont())   return {};
        if (typefaceName == getDefaultSans())   return "juce::Font::getDefaultSansSerifFontName(), ";
        if (typefaceName == getDefaultSerif())  return "juce::Font::getDefaultSerifFontName(), ";
        if (typefaceName == getDefaultMono())   return "juce::Font::getDefaultMonospacedFontName(), ";

        return "\"" + typefaceName + "\", ";
    }

    static String getFontStyleCode (const Font& font)
    {
        if (font.isBold() && font.isItalic())   return "juce::Font::bold | juce::Font::italic";
        if (font.isBold())                      return "juce::Font::bold";
        if (font.isItalic())                    return "juce::Font::italic";

        return "juce::Font::plain";
    }

    /** If typefaceNameCode is supplied, it's used as the C++ expression for the typeface name
        instead of the name itself (used for the project's embedded fonts).
    */
    static String getCompleteFontCode (const Font& font, const String& typefaceName,
                                       const String& typefaceNameCode = {})
    {
        String s;

        s << "juce::Font (juce::FontOptions { "
          << (typefaceNameCode.isNotEmpty() ? typefaceNameCode + ", " : getTypefaceNameCode (typefaceName))
          << CodeHelpers::floatLiteral (font.getHeight(), 2)
          << ", ";

        if (font.getAvailableStyles().contains (font.getTypefaceStyle()))
            s << "juce::Font::plain }.withStyle ("
              << CodeHelpers::stringLiteral (font.getTypefaceStyle())
              << ")";
        else
            s << getFontStyleCode (font)
              << " }";

        s << ".withMetricsKind (juce::TypefaceMetricsKind::legacy)";

        if (! approximatelyEqual (font.getExtraKerningFactor(), 0.0f))
            s << ".withKerningFactor ("
              << CodeHelpers::floatLiteral (font.getExtraKerningFactor(), 3)
              << ")";

        s << ")";

        return s;
    }

private:
    static std::map<String, Typeface::Ptr>& getTypefaceCache()
    {
        static std::map<String, Typeface::Ptr> cache;
        return cache;
    }

    static Array<File> getProjectFontFiles (Project* project)
    {
        Array<File> files;

        for (auto& f : getProjectBinaryResources (project))
            if (isFontFileName (f.getFileName()))
                files.add (f);

        return files;
    }

    /** All of the project's binary resources, in the order that JucerResourceFile adds them. */
    static Array<File> getProjectBinaryResources (Project* project)
    {
        Array<File> files;

        if (project != nullptr)
            findBinaryResources (project->getMainGroup(), files);

        return files;
    }

    static void findBinaryResources (const Project::Item& item, Array<File>& results)
    {
        if (item.isGroup())
        {
            for (int i = 0; i < item.getNumChildren(); ++i)
                findBinaryResources (item.getChild (i), results);
        }
        else if (item.shouldBeAddedToBinaryResources())
        {
            results.add (item.getFile());
        }
    }

    static Font makeLegacyFont (const String& typefaceName, float height, int styleFlags, float extraKerning)
    {
        return Font { FontOptions { typefaceName, height, styleFlags }
                          .withMetricsKind (TypefaceMetricsKind::legacy)
                          .withKerningFactor (extraKerning) };
    }
};
