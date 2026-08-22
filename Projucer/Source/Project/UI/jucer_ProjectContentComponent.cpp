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

#include "../../Application/jucer_Headers.h"
#include "jucer_ProjectContentComponent.h"

#include "Sidebar/jucer_Sidebar.h"
#include "../../Terminal/jucer_TerminalPanel.h"
#include "../../AI/jucer_AiChatView.h"
#include "../../AI/jucer_AiSession.h"
#include "../../AI/jucer_CodexAuth.h"

struct WizardHolder
{
    std::unique_ptr<NewFileWizard::Type> wizard;
};

NewFileWizard::Type* createGUIComponentWizard (Project&);

namespace
{
constexpr auto terminalPanelHeightProperty = "terminalPanelHeight";
constexpr int minimumMainContentHeight = 100;
constexpr int maximumTerminalPanelHeight = 4000;
}

//==============================================================================
ProjectContentComponent::ProjectContentComponent()
    : sidebar (std::make_unique<Sidebar> (project))
{
    setOpaque (true);
    setWantsKeyboardFocus (true);

    addAndMakeVisible (headerComponent);
    addAndMakeVisible (projectMessagesComponent);
    addAndMakeVisible (contentViewComponent);

    sidebarSizeConstrainer.setMinimumWidth (200);
    sidebarSizeConstrainer.setMaximumWidth (500);

    ProjucerApplication::getApp().openDocumentManager.addListener (this);

    getGlobalProperties().addChangeListener (this);
}

ProjectContentComponent::~ProjectContentComponent()
{
    getGlobalProperties().removeChangeListener (this);
    ProjucerApplication::getApp().openDocumentManager.removeListener (this);

    setProject (nullptr);
    discardAiSession();
    removeChildComponent (&bubbleMessage);
}

void ProjectContentComponent::paint (Graphics& g)
{
    g.fillAll (findColour (backgroundColourId));
}

void ProjectContentComponent::resized()
{
    auto r = getLocalBounds();

    r.removeFromRight (10);
    r.removeFromLeft (15);
    r.removeFromTop (5);

    projectMessagesComponent.setBounds (r.removeFromBottom (40).withWidth (100).reduced (0, 5));
    headerComponent.setBounds (r.removeFromTop (40));

    r.removeFromTop (10);

    if (terminalPanel != nullptr && terminalPanel->isVisible())
    {
        const auto maximumHeight = jmax (TerminalPanel::minimumHeight, r.getHeight() - minimumMainContentHeight);
        const auto height = jlimit (TerminalPanel::minimumHeight,
                                    maximumHeight,
                                    terminalPanel->getHeight());

        auto terminalArea = r.removeFromBottom (height);
        terminalPanel->setBounds (terminalArea);
        terminalResizerBar->setBounds (terminalArea.getX(), terminalArea.getY() - 2,
                                       terminalArea.getWidth(), 5);
    }

    auto sidebarArea = r.removeFromLeft (sidebar != nullptr && sidebar->getWidth() != 0 ? sidebar->getWidth()
                                                                                        : r.getWidth() / 4);

    if (sidebar != nullptr && sidebar->isVisible())
        sidebar->setBounds (sidebarArea);

    if (resizerBar != nullptr)
        resizerBar->setBounds (r.withWidth (4));

    contentViewComponent.setBounds (r);

    headerComponent.sidebarTabsWidthChanged (sidebarArea.getWidth());
}

void ProjectContentComponent::lookAndFeelChanged()
{
    repaint();

    if (translationTool != nullptr)
        translationTool->repaint();
}

void ProjectContentComponent::childBoundsChanged (Component* child)
{
    if (child == terminalPanel.get())
        getGlobalProperties().setValue (terminalPanelHeightProperty, terminalPanel->getHeight());

    if (child == sidebar.get() || child == terminalPanel.get())
        resized();
}

void ProjectContentComponent::setProject (Project* newProject)
{
    if (project != newProject)
    {
        if (project != nullptr)
            project->removeChangeListener (this);

        discardAiSession();
        hideEditor();
        terminalResizerBar = nullptr;
        terminalPanel = nullptr;
        resizerBar = nullptr;
        sidebar = nullptr;

        project = newProject;

        if (project != nullptr)
        {
            sidebar = std::make_unique<Sidebar> (project);
            addAndMakeVisible (sidebar.get());

            //==============================================================================
            resizerBar = std::make_unique<ResizableEdgeComponent> (sidebar.get(), &sidebarSizeConstrainer,
                                                                   ResizableEdgeComponent::rightEdge);
            addAndMakeVisible (resizerBar.get());
            resizerBar->setAlwaysOnTop (true);

            project->addChangeListener (this);

            updateMissingFileStatuses();

            headerComponent.setVisible (true);
            headerComponent.setCurrentProject (project);

            projectMessagesComponent.setVisible (true);
        }
        else
        {
            headerComponent.setVisible (false);
            projectMessagesComponent.setVisible (false);
        }

        projectMessagesComponent.setProject (project);

        resized();
    }
}

void ProjectContentComponent::saveOpenDocumentList()
{
    if (project != nullptr)
    {
        std::unique_ptr<XmlElement> xml (recentDocumentList.createXML());

        if (xml != nullptr)
            project->getStoredProperties().setValue ("lastDocs", xml.get());
    }
}

void ProjectContentComponent::reloadLastOpenDocuments()
{
    if (project != nullptr)
    {
        if (auto xml = project->getStoredProperties().getXmlValue ("lastDocs"))
        {
            recentDocumentList.restoreFromXML (*project, *xml);
            showDocument (recentDocumentList.getCurrentDocument(), true);
        }
    }
}

bool ProjectContentComponent::documentAboutToClose (OpenDocumentManager::Document* document)
{
    if (aiReturnDocument == document)
        aiReturnDocument = nullptr;

    hideDocument (document);
    return true;
}

void ProjectContentComponent::changeListenerCallback (ChangeBroadcaster* broadcaster)
{
    if (broadcaster == project)
        updateMissingFileStatuses();
}

void ProjectContentComponent::refreshProjectTreeFileStatuses()
{
    if (sidebar != nullptr)
        if (auto* fileTree = sidebar->getFileTreePanel())
            fileTree->repaint();
}

void ProjectContentComponent::updateMissingFileStatuses()
{
    if (sidebar != nullptr)
        if (auto* tree = sidebar->getFileTreePanel())
            tree->updateMissingFileStatuses();
}

bool ProjectContentComponent::showEditorForFile (const File& fileToShow, bool grabFocus)
{
    if (getCurrentFile() != fileToShow)
        return showDocument (ProjucerApplication::getApp().openDocumentManager.openFile (project, fileToShow), grabFocus);

    if (aiViewShowing)
        return showDocument (currentDocument, grabFocus);

    return true;
}

bool ProjectContentComponent::hasFileInRecentList (const File& f) const
{
    return recentDocumentList.contains (f);
}

File ProjectContentComponent::getCurrentFile() const
{
    return currentDocument != nullptr ? currentDocument->getFile()
                                      : File();
}

bool ProjectContentComponent::showDocument (OpenDocumentManager::Document* doc, bool grabFocus)
{
    if (doc == nullptr)
    {
        leaveAiView();
        return false;
    }

    const auto wasShowingAiView = aiViewShowing;
    aiViewShowing = false;

    if (doc->hasFileBeenModifiedExternally())
        doc->reloadFromFile();

    if (doc != getCurrentDocument() || wasShowingAiView)
    {
        recentDocumentList.newDocumentOpened (doc);
        setEditorDocument (doc->createEditor(), doc);
    }

    if (grabFocus && contentViewComponent.isShowing())
        contentViewComponent.grabKeyboardFocus();

    return true;
}

void ProjectContentComponent::hideEditor()
{
    aiViewShowing = false;
    aiReturnDocument = nullptr;
    currentDocument = nullptr;
    contentViewComponent.setContent ({}, {});

    ProjucerApplication::getCommandManager().commandStatusChanged();
    resized();
}

void ProjectContentComponent::hideDocument (OpenDocumentManager::Document* doc)
{
    if (doc != currentDocument)
        return;

    if (auto* replacement = recentDocumentList.getClosestPreviousDocOtherThan (currentDocument))
        showDocument (replacement, true);
    else
        hideEditor();
}

void ProjectContentComponent::setScrollableEditorComponent (std::unique_ptr<Component> component)
{
    jassert (component.get() != nullptr);

    if (aiViewShowing && currentDocument != nullptr)
        aiReturnDocument = currentDocument;

    aiViewShowing = false;
    class ContentViewport final : public Component
    {
    public:
        ContentViewport (std::unique_ptr<Component> content)
        {
            contentViewport.setViewedComponent (content.release(), true);
            addAndMakeVisible (contentViewport);
        }

        void resized() override
        {
            contentViewport.setBounds (getLocalBounds());
        }

    private:
        Viewport contentViewport;
    };

    contentViewComponent.setContent (std::make_unique<ContentViewport> (std::move (component)), {});
    currentDocument = nullptr;

    ProjucerApplication::getCommandManager().commandStatusChanged();
}

void ProjectContentComponent::setEditorDocument (std::unique_ptr<Component> component, OpenDocumentManager::Document* doc)
{
    aiViewShowing = false;
    aiReturnDocument = nullptr;
    currentDocument = doc;
    contentViewComponent.setContent (std::move (component),
                                     currentDocument != nullptr ? currentDocument->getFile().getFileName()
                                                                : String());

    ProjucerApplication::getCommandManager().commandStatusChanged();
}

void ProjectContentComponent::toggleAiView()
{
    if (project == nullptr)
        return;

    if (aiViewShowing)
    {
        leaveAiView();
        return;
    }

    if (codexAuth == nullptr)
        codexAuth = std::make_shared<CodexAuth>();

    if (aiSession == nullptr)
        aiSession = std::make_shared<AiSession> (codexAuth, project->getProjectFolder());

    if (currentDocument != nullptr)
        aiReturnDocument = currentDocument;

    aiViewShowing = true;
    contentViewComponent.setContent (std::make_unique<AiChatView> (aiSession, codexAuth),
                                     "AI Assistant");
}

void ProjectContentComponent::leaveAiView()
{
    if (! aiViewShowing)
        return;

    aiViewShowing = false;

    auto* document = currentDocument != nullptr ? currentDocument : aiReturnDocument;
    aiReturnDocument = nullptr;

    if (document != nullptr)
    {
        if (document->hasFileBeenModifiedExternally())
            document->reloadFromFile();

        setEditorDocument (document->createEditor(), document);
    }
    else
    {
        hideEditor();
    }
}

void ProjectContentComponent::discardAiSession()
{
    if (aiSession != nullptr)
        aiSession->stop();

    // The view holds references to the session and auth objects, so clear the
    // content before releasing either owner.
    if (aiViewShowing)
    {
        aiViewShowing = false;
        contentViewComponent.setContent ({}, {});
    }

    aiReturnDocument = nullptr;

    aiSession.reset();
    codexAuth.reset();
}

Component* ProjectContentComponent::getEditorComponent()
{
    return contentViewComponent.getCurrentComponent();
}

void ProjectContentComponent::closeDocument()
{
    if (currentDocument != nullptr)
    {
        ProjucerApplication::getApp().openDocumentManager
                                     .closeDocumentAsync (currentDocument, OpenDocumentManager::SaveIfNeeded::yes, nullptr);
        return;
    }

    if (! goToPreviousFile())
        hideEditor();
}

static ScopedMessageBox showSaveWarning (OpenDocumentManager::Document* currentDocument)
{
    auto options = MessageBoxOptions::makeOptionsOk (MessageBoxIconType::WarningIcon,
                                                     TRANS ("Save failed!"),
                                                     TRANS ("Couldn't save the file:")
                                                         + "\n" + currentDocument->getFile().getFullPathName());
    return AlertWindow::showScopedAsync (options, nullptr);
}

void ProjectContentComponent::saveDocumentAsync()
{
    if (currentDocument != nullptr)
    {
        currentDocument->saveAsync ([parent = SafePointer { this }] (bool savedSuccessfully)
        {
            if (parent == nullptr)
                return;

            if (! savedSuccessfully)
                parent->messageBox = showSaveWarning (parent->currentDocument);

            parent->refreshProjectTreeFileStatuses();
        });
    }
    else
    {
        saveProjectAsync();
    }
}

void ProjectContentComponent::saveAsAsync()
{
    if (currentDocument != nullptr)
    {
        currentDocument->saveAsAsync ([parent = SafePointer { this }] (bool savedSuccessfully)
        {
            if (parent == nullptr)
                return;

            if (! savedSuccessfully)
                parent->messageBox = showSaveWarning (parent->currentDocument);

            parent->refreshProjectTreeFileStatuses();
        });
    }
}

bool ProjectContentComponent::goToPreviousFile()
{
    auto* doc = recentDocumentList.getCurrentDocument();

    if (doc == nullptr || doc == getCurrentDocument())
        doc = recentDocumentList.getPrevious();

    return showDocument (doc, true);
}

bool ProjectContentComponent::goToNextFile()
{
    return showDocument (recentDocumentList.getNext(), true);
}

bool ProjectContentComponent::canGoToCounterpart() const
{
    return currentDocument != nullptr
            && currentDocument->getCounterpartFile().exists();
}

bool ProjectContentComponent::goToCounterpart()
{
    if (currentDocument != nullptr)
    {
        auto file = currentDocument->getCounterpartFile();

        if (file.exists())
            return showEditorForFile (file, true);
    }

    return false;
}

void ProjectContentComponent::saveProjectAsync()
{
    if (project == nullptr)
        return;

    if (project->isTemporaryProject())
        project->saveAndMoveTemporaryProject (false);
    else
        project->saveAsync (true, true, nullptr);
}

void ProjectContentComponent::closeProject()
{
    if (auto* mw = findParentComponentOfClass<MainWindow>())
        mw->closeCurrentProject (OpenDocumentManager::SaveIfNeeded::yes, nullptr);
}

void ProjectContentComponent::showProjectSettings()
{
    setScrollableEditorComponent (std::make_unique<ProjectSettingsComponent> (*project));
}

void ProjectContentComponent::showCurrentExporterSettings()
{
    if (auto selected = headerComponent.getSelectedExporter())
        showExporterSettings (selected->getUniqueName());
}

void ProjectContentComponent::showExporterSettings (const String& exporterName)
{
    if (exporterName.isEmpty())
        return;

    showExportersPanel();

    if (sidebar == nullptr)
        return;

    if (auto* exportersPanel = sidebar->getExportersTreePanel())
    {
        if (auto* exporters = dynamic_cast<TreeItemTypes::ExportersTreeRoot*> (exportersPanel->rootItem.get()))
        {
            for (auto i = exporters->getNumSubItems(); i >= 0; --i)
            {
                if (auto* e = dynamic_cast<TreeItemTypes::ExporterItem*> (exporters->getSubItem (i)))
                {
                    if (e->getDisplayName() == exporterName)
                    {
                        if (e->isSelected())
                            e->setSelected (false, true);

                        e->setSelected (true, true);
                    }
                }
            }
        }
    }
}

void ProjectContentComponent::showModule (const String& moduleID)
{
    showModulesPanel();

    if (sidebar == nullptr)
        return;

    if (auto* modsPanel = sidebar->getModuleTreePanel())
    {
        if (auto* mods = dynamic_cast<TreeItemTypes::EnabledModulesItem*> (modsPanel->rootItem.get()))
        {
            for (auto i = mods->getNumSubItems(); --i >= 0;)
            {
                if (auto* m = dynamic_cast<TreeItemTypes::ModuleItem*> (mods->getSubItem (i)))
                {
                    if (m->moduleID == moduleID)
                    {
                        if (m->isSelected())
                            m->setSelected (false, true);

                        m->setSelected (true, true);
                    }
                }
            }
        }
    }
}

StringArray ProjectContentComponent::getExportersWhichCanLaunch() const
{
    StringArray s;

    if (project != nullptr)
        for (Project::ExporterIterator exporter (*project); exporter.next();)
            if (exporter->canLaunchProject())
                s.add (exporter->getUniqueName());

    return s;
}

void ProjectContentComponent::openInSelectedIDE (bool saveFirst)
{
    if (project == nullptr)
        return;

    if (auto selectedExporter = headerComponent.getSelectedExporter())
    {
        if (saveFirst)
        {
            if (project->isTemporaryProject())
            {
                project->saveAndMoveTemporaryProject (true);
                return;
            }

            SafePointer<ProjectContentComponent> safeThis { this };
            project->saveAsync (true, true, [safeThis] (Project::SaveResult r)
                                {
                                    if (safeThis != nullptr && r == Project::SaveResult::savedOk)
                                        safeThis->openInSelectedIDE (false);
                                });
            return;
        }

        project->openProjectInIDE (*selectedExporter);
    }
}

void ProjectContentComponent::showNewExporterMenu()
{
    if (project != nullptr)
    {
        PopupMenu menu;

        menu.addSectionHeader ("Create a new export target:");

        SafePointer<ProjectContentComponent> safeThis (this);

        for (auto& exporterInfo : ProjectExporter::getExporterTypeInfos())
        {
            PopupMenu::Item item;

            item.itemID = -1;
            item.text = exporterInfo.displayName;

            item.image = [exporterInfo]
            {
                auto drawableImage = std::make_unique<DrawableImage>();
                drawableImage->setImage (exporterInfo.icon);

                return drawableImage;
            }();

            item.action = [safeThis, exporterInfo]
            {
                if (safeThis != nullptr)
                    if (auto* p = safeThis->getProject())
                        p->addNewExporter (exporterInfo.identifier);
            };

            menu.addItem (item);
        }

        menu.showMenuAsync ({});
    }
}

void ProjectContentComponent::deleteSelectedTreeItems()
{
    if (sidebar != nullptr)
        if (auto* tree = sidebar->getTreeWithSelectedItems())
            tree->deleteSelectedItems();
}

void ProjectContentComponent::showBubbleMessage (Rectangle<int> pos, const String& text)
{
    addChildComponent (bubbleMessage);
    bubbleMessage.setColour (BubbleComponent::backgroundColourId, Colours::white.withAlpha (0.7f));
    bubbleMessage.setColour (BubbleComponent::outlineColourId, Colours::black.withAlpha (0.8f));
    bubbleMessage.setAlwaysOnTop (true);

    bubbleMessage.showAt (pos, AttributedString (text), 3000, true, false);
}

//==============================================================================
void ProjectContentComponent::showTranslationTool()
{
    if (translationTool != nullptr)
    {
        translationTool->toFront (true);
    }
    else if (project != nullptr)
    {
        new FloatingToolWindow ("Translation File Builder",
                                "transToolWindowPos",
                                new TranslationToolComponent(),
                                translationTool, true,
                                600, 700,
                                600, 400, 10000, 10000);
    }
}

//==============================================================================
struct AsyncCommandRetrier final : public Timer
{
    AsyncCommandRetrier (const ApplicationCommandTarget::InvocationInfo& i)  : info (i)
    {
        info.originatingComponent = nullptr;
        startTimer (500);
    }

    void timerCallback() override
    {
        stopTimer();
        ProjucerApplication::getCommandManager().invoke (info, true);
        delete this;
    }

    ApplicationCommandTarget::InvocationInfo info;

    JUCE_DECLARE_NON_COPYABLE (AsyncCommandRetrier)
};

static bool reinvokeCommandAfterCancellingModalComps (const ApplicationCommandTarget::InvocationInfo& info)
{
    if (ModalComponentManager::getInstance()->cancelAllModalComponents())
    {
        new AsyncCommandRetrier (info);
        return true;
    }

    return false;
}

//==============================================================================
ApplicationCommandTarget* ProjectContentComponent::getNextCommandTarget()
{
    return findFirstTargetParentComponent();
}

void ProjectContentComponent::getAllCommands (Array <CommandID>& commands)
{
    commands.addArray ({ CommandIDs::saveProject,
                         CommandIDs::closeProject,
                         CommandIDs::saveDocument,
                         CommandIDs::saveDocumentAs,
                         CommandIDs::closeDocument,
                         CommandIDs::goToPreviousDoc,
                         CommandIDs::goToNextDoc,
                         CommandIDs::goToCounterpart,
                         CommandIDs::showProjectSettings,
                         CommandIDs::showFileExplorerPanel,
                         CommandIDs::showModulesPanel,
                         CommandIDs::showExportersPanel,
                         CommandIDs::showExporterSettings,
                         CommandIDs::showTerminal,
                         CommandIDs::openInIDE,
                         CommandIDs::saveAndOpenInIDE,
                         CommandIDs::createNewExporter,
                         CommandIDs::deleteSelectedItem,
                         CommandIDs::showTranslationTool,
                         CommandIDs::addNewGUIFile });
}

void ProjectContentComponent::getCommandInfo (const CommandID commandID, ApplicationCommandInfo& result)
{
    String documentName;
    if (currentDocument != nullptr)
        documentName = " '" + currentDocument->getName().substring (0, 32) + "'";

   #if JUCE_MAC
    auto cmdCtrl = (ModifierKeys::ctrlModifier | ModifierKeys::commandModifier);
   #else
    auto cmdCtrl = (ModifierKeys::ctrlModifier | ModifierKeys::altModifier);
   #endif

    switch (commandID)
    {
    case CommandIDs::saveProject:
        result.setInfo ("Save Project",
                        "Saves the current project",
                        CommandCategories::general, 0);
        result.setActive (project != nullptr && ! project->isSaveAndExportDisabled() && ! project->isCurrentlySaving());
        result.defaultKeypresses.add ({ 'p', ModifierKeys::commandModifier, 0 });
        break;

    case CommandIDs::closeProject:
        result.setInfo ("Close Project",
                        "Closes the current project",
                        CommandCategories::general, 0);
        result.setActive (project != nullptr);
        break;

    case CommandIDs::saveDocument:
        result.setInfo ("Save" + documentName,
                        "Saves the current document",
                        CommandCategories::general, 0);
        result.setActive (currentDocument != nullptr || (project != nullptr && ! project->isCurrentlySaving()));
        result.defaultKeypresses.add ({ 's', ModifierKeys::commandModifier, 0 });
        break;

    case CommandIDs::saveDocumentAs:
        result.setInfo ("Save As...",
                        "Saves the current document to a new location",
                        CommandCategories::general, 0);
        result.setActive (currentDocument != nullptr);
        result.defaultKeypresses.add ({ 's', ModifierKeys::commandModifier | ModifierKeys::shiftModifier, 0 });
        break;

    case CommandIDs::closeDocument:
        result.setInfo ("Close" + documentName,
                        "Closes the current document",
                        CommandCategories::general, 0);
        result.setActive (currentDocument != nullptr);
        result.defaultKeypresses.add ({ 'w', cmdCtrl, 0 });
        break;

    case CommandIDs::goToPreviousDoc:
        result.setInfo ("Previous Document",
                        "Go to previous document",
                        CommandCategories::general, 0);
        result.setActive (recentDocumentList.canGoToPrevious());
        result.defaultKeypresses.add ({ KeyPress::leftKey, cmdCtrl, 0 });
        break;

    case CommandIDs::goToNextDoc:
        result.setInfo ("Next Document",
                        "Go to next document",
                        CommandCategories::general, 0);
        result.setActive (recentDocumentList.canGoToNext());
        result.defaultKeypresses.add ({ KeyPress::rightKey, cmdCtrl, 0 });
        break;

    case CommandIDs::goToCounterpart:
        result.setInfo ("Open Counterpart File",
                        "Open corresponding header or cpp file",
                        CommandCategories::general, 0);
        result.setActive (canGoToCounterpart());
        result.defaultKeypresses.add ({ KeyPress::upKey, cmdCtrl, 0 });
        break;

    case CommandIDs::showProjectSettings:
        result.setInfo ("Show Project Settings",
                        "Shows the main project options page",
                        CommandCategories::general, 0);
        result.setActive (project != nullptr);
        result.defaultKeypresses.add ({ 'x', cmdCtrl, 0 });
        break;

    case CommandIDs::showFileExplorerPanel:
        result.setInfo ("Show File Explorer Panel",
                        "Shows the panel containing the tree of files for this project",
                        CommandCategories::general, 0);
        result.setActive (project != nullptr);
        result.defaultKeypresses.add ({ 'f', cmdCtrl, 0 });
        break;

    case CommandIDs::showModulesPanel:
        result.setInfo ("Show Modules Panel",
                        "Shows the panel containing the project's list of modules",
                        CommandCategories::general, 0);
        result.setActive (project != nullptr);
        result.defaultKeypresses.add ({ 'm', cmdCtrl, 0 });
        break;

    case CommandIDs::showExportersPanel:
        result.setInfo ("Show Exporters Panel",
                        "Shows the panel containing the project's list of exporters",
                        CommandCategories::general, 0);
        result.setActive (project != nullptr);
        result.defaultKeypresses.add ({ 'e', cmdCtrl, 0 });
        break;

    case CommandIDs::showExporterSettings:
        result.setInfo ("Show Exporter Settings",
                        "Shows the settings page for the currently selected exporter",
                        CommandCategories::general, 0);
        result.setActive (project != nullptr);
        result.defaultKeypresses.add ({ 'e', ModifierKeys::commandModifier | ModifierKeys::shiftModifier, 0 });
        break;

    case CommandIDs::showTerminal:
        result.setInfo ("Show Terminal",
                        "Shows or hides the terminal panel at the bottom of the window",
                        CommandCategories::general, 0);
        result.setActive (project != nullptr);
        result.setTicked (isTerminalVisible());
        result.defaultKeypresses.add ({ '`', cmdCtrl, 0 });
        break;

    case CommandIDs::openInIDE:
        result.setInfo ("Open in IDE...",
                        "Launches the project in an external IDE",
                        CommandCategories::general, 0);
        result.setActive (ProjectExporter::canProjectBeLaunched (project) && ! project->isSaveAndExportDisabled());
        break;

    case CommandIDs::saveAndOpenInIDE:
        result.setInfo ("Save Project and Open in IDE...",
                        "Saves the project and launches it in an external IDE",
                        CommandCategories::general, 0);
        result.setActive (ProjectExporter::canProjectBeLaunched (project) && ! project->isSaveAndExportDisabled() && ! project->isCurrentlySaving());
        result.defaultKeypresses.add ({ 'l', ModifierKeys::commandModifier | ModifierKeys::shiftModifier, 0 });
        break;

    case CommandIDs::createNewExporter:
        result.setInfo ("Create New Exporter...",
                        "Creates a new exporter for a compiler type",
                        CommandCategories::general, 0);
        result.setActive (project != nullptr);
        break;

    case CommandIDs::deleteSelectedItem:
        result.setInfo ("Delete Selected File",
                        String(),
                        CommandCategories::general, 0);
        result.defaultKeypresses.add ({ KeyPress::deleteKey, 0, 0 });
        result.defaultKeypresses.add ({ KeyPress::backspaceKey, 0, 0 });
        break;

    case CommandIDs::showTranslationTool:
        result.setInfo ("Translation File Builder",
                        "Shows the translation file helper tool",
                        CommandCategories::general, 0);
        break;

    case CommandIDs::addNewGUIFile:
        result.setInfo ("Add new GUI Component...",
                        "Adds a new GUI Component file to the project",
                        CommandCategories::general,
                        (! ProjucerApplication::getApp().isGUIEditorEnabled() ? ApplicationCommandInfo::isDisabled : 0));
        break;

    default:
        break;
    }
}

bool ProjectContentComponent::perform (const InvocationInfo& info)
{
    // don't allow the project to be saved again if it's currently saving
    if (isSaveCommand (info.commandID) && project != nullptr && project->isCurrentlySaving())
        return false;

    switch (info.commandID)
    {
        case CommandIDs::saveProject:
        case CommandIDs::closeProject:
        case CommandIDs::saveDocument:
        case CommandIDs::saveDocumentAs:
        case CommandIDs::closeDocument:
        case CommandIDs::goToPreviousDoc:
        case CommandIDs::goToNextDoc:
        case CommandIDs::goToCounterpart:
        case CommandIDs::saveAndOpenInIDE:
            if (reinvokeCommandAfterCancellingModalComps (info))
            {
                grabKeyboardFocus(); // to force any open labels to close their text editors
                return true;
            }

            break;

        default:
            break;
    }

    if (isCurrentlyBlockedByAnotherModalComponent())
        return false;

    switch (info.commandID)
    {
        case CommandIDs::saveProject:               saveProjectAsync();             break;
        case CommandIDs::closeProject:              closeProject();                 break;
        case CommandIDs::saveDocument:              saveDocumentAsync();            break;
        case CommandIDs::saveDocumentAs:            saveAsAsync();                  break;
        case CommandIDs::closeDocument:             closeDocument();                break;
        case CommandIDs::goToPreviousDoc:           goToPreviousFile();             break;
        case CommandIDs::goToNextDoc:               goToNextFile();                 break;
        case CommandIDs::goToCounterpart:           goToCounterpart();              break;

        case CommandIDs::showProjectSettings:       showProjectSettings();          break;
        case CommandIDs::showFileExplorerPanel:     showFilesPanel();               break;
        case CommandIDs::showModulesPanel:          showModulesPanel();             break;
        case CommandIDs::showExportersPanel:        showExportersPanel();           break;
        case CommandIDs::showExporterSettings:      showCurrentExporterSettings();  break;
        case CommandIDs::showTerminal:              toggleTerminal();               break;

        case CommandIDs::openInIDE:                 openInSelectedIDE (false);      break;
        case CommandIDs::saveAndOpenInIDE:          openInSelectedIDE (true);       break;

        case CommandIDs::createNewExporter:         showNewExporterMenu();          break;

        case CommandIDs::deleteSelectedItem:        deleteSelectedTreeItems();      break;

        case CommandIDs::showTranslationTool:       showTranslationTool();          break;

        case CommandIDs::addNewGUIFile:             addNewGUIFile();                break;

        default:
            return false;
    }

    return true;
}

bool ProjectContentComponent::isSaveCommand (const CommandID id)
{
    return (id == CommandIDs::saveProject || id == CommandIDs::saveDocument || id == CommandIDs::saveAndOpenInIDE);
}

void ProjectContentComponent::getSelectedProjectItemsBeingDragged (const DragAndDropTarget::SourceDetails& dragSourceDetails,
                                                                   OwnedArray<Project::Item>& selectedNodes)
{
    TreeItemTypes::FileTreeItemBase::getSelectedProjectItemsBeingDragged (dragSourceDetails, selectedNodes);
}

void ProjectContentComponent::addNewGUIFile()
{
    if (project != nullptr)
    {
        wizardHolder = std::make_unique<WizardHolder>();
        wizardHolder->wizard.reset (createGUIComponentWizard (*project));
        wizardHolder->wizard->createNewFile (*project, project->getMainGroup());
    }
}

bool ProjectContentComponent::isTerminalVisible() const
{
    return terminalPanel != nullptr && terminalPanel->isVisible();
}

void ProjectContentComponent::toggleTerminal()
{
    if (project == nullptr)
        return;

    if (terminalPanel == nullptr)
    {
        terminalPanel = std::make_unique<TerminalPanel> (project->getProjectFolder());
        terminalPanel->setSize (getWidth(),
                                getGlobalProperties().getIntValue (terminalPanelHeightProperty,
                                                                   TerminalPanel::defaultHeight));
        addAndMakeVisible (terminalPanel.get());

        terminalSizeConstrainer.setMinimumHeight (TerminalPanel::minimumHeight);
        terminalSizeConstrainer.setMaximumHeight (maximumTerminalPanelHeight);

        terminalResizerBar = std::make_unique<ResizableEdgeComponent> (terminalPanel.get(),
                                                                      &terminalSizeConstrainer,
                                                                      ResizableEdgeComponent::topEdge);
        addAndMakeVisible (terminalResizerBar.get());
        terminalResizerBar->setAlwaysOnTop (true);
    }
    else
    {
        const auto nowVisible = ! terminalPanel->isVisible();
        terminalPanel->setVisible (nowVisible);
        terminalResizerBar->setVisible (nowVisible);
    }

    resized();

    if (isTerminalVisible())
        terminalPanel->focusCurrentTerminal();
}

//==============================================================================
void ProjectContentComponent::showProjectPanel (const int index)
{
    leaveAiView();

    if (sidebar != nullptr)
        sidebar->showPanel (index);
}
