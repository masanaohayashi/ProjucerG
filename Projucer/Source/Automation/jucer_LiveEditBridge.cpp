/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   See LICENSE.md for the terms that apply to this repository.

  ==============================================================================
*/

#include "../Application/jucer_Application.h"
#include "../Automation/jucer_GuiDocumentAdapter.h"
#include "../ComponentEditor/UI/jucer_JucerDocumentEditor.h"
#include "../ComponentEditor/UI/jucer_ComponentLayoutPanel.h"
#include "../Project/UI/jucer_ProjectContentComponent.h"
#include "../Application/jucer_MainWindow.h"

namespace ProjucerAutomation
{
namespace
{
    static var makeErrorResponse (int id, int code, const String& message)
    {
        auto response = std::make_unique<DynamicObject>();
        response->setProperty ("jsonrpc", "2.0");
        response->setProperty ("id", id);

        auto error = std::make_unique<DynamicObject>();
        error->setProperty ("code", code);
        error->setProperty ("message", message);

        response->setProperty ("error", var (error.release()));
        return var (response.release());
    }

    static var makeResultResponse (int id, const var& result)
    {
        auto response = std::make_unique<DynamicObject>();
        response->setProperty ("jsonrpc", "2.0");
        response->setProperty ("id", id);
        response->setProperty ("result", result);
        return var (response.release());
    }

    static String toJsonString (const var& value)
    {
        return JSON::toString (value);
    }

    static File sessionDirectory()
    {
        auto dir = File::getSpecialLocation (File::SpecialLocationType::userApplicationDataDirectory)
                         .getChildFile ("Projucer");
        dir.createDirectory();
        return dir;
    }

static JucerDocumentEditor* findEditorInDesktop()
    {
        return JucerDocumentEditor::getActiveDocumentHolder();
    }

    static bool matchesDocumentFile (JucerDocument& document, const File& file)
    {
        if (file == document.getCppFile())
            return true;

        if (auto* project = document.getCppDocument().getProject())
            if (file == project->getFile())
                return true;

        return false;
    }
}

File getLiveEditSessionInfoFile()
{
    return sessionDirectory().getChildFile ("live-edit-session.json");
}

bool readLiveEditSessionInfo (LiveEditSessionInfo& info)
{
    auto file = getLiveEditSessionInfoFile();

    if (! file.existsAsFile())
        return false;

    auto data = JSON::parse (file);
    auto* object = data.getDynamicObject();

    if (object == nullptr)
        return false;

    info.port = (int) object->getProperty ("port");
    info.token = object->getProperty ("token").toString();
    return info.port > 0 && info.token.isNotEmpty();
}

LiveEditBridge::LiveEditBridge (ProjucerApplication& application)
    : app (application)
{
}

LiveEditBridge::~LiveEditBridge()
{
    stop();
}

void LiveEditBridge::start()
{
    if (running)
        return;

    deleteSessionInfo();

    if (! beginWaitingForSocket (0, "127.0.0.1"))
        return;

    sessionInfo.port = getBoundPort();
    sessionInfo.token = createToken();
    sessionInfoFile = getLiveEditSessionInfoFile();
    writeSessionInfo();
    running = true;
}

void LiveEditBridge::stop()
{
    deleteSessionInfo();

    if (! running)
        return;

    running = false;
    InterprocessConnectionServer::stop();
}

String LiveEditBridge::createToken()
{
    return Uuid().toString();
}

void LiveEditBridge::writeSessionInfo()
{
    auto object = std::make_unique<DynamicObject>();
    object->setProperty ("port", sessionInfo.port);
    object->setProperty ("token", sessionInfo.token);

    sessionInfoFile.replaceWithText (JSON::toString (var (object.release())));
}

void LiveEditBridge::deleteSessionInfo()
{
    if (sessionInfoFile.existsAsFile())
        sessionInfoFile.deleteFile();
}

InterprocessConnection* LiveEditBridge::createConnectionObject()
{
    return new Connection (*this);
}

LiveEditBridge::Connection::Connection (LiveEditBridge& bridgeToUse)
    : InterprocessConnection (false),
      bridge (bridgeToUse)
{
}

LiveEditBridge::Connection::~Connection()
{
    cancelPendingUpdate();
    disconnect();
}

void LiveEditBridge::Connection::connectionMade()
{
}

void LiveEditBridge::Connection::connectionLost()
{
}

void LiveEditBridge::Connection::sendResponse (const var& response)
{
    auto text = toJsonString (response);
    MemoryBlock message (text.toRawUTF8(), (size_t) text.getNumBytesAsUTF8());
    sendMessage (message);
}

void LiveEditBridge::Connection::sendError (int id, int code, const String& message)
{
    sendResponse (makeErrorResponse (id, code, message));
}

void LiveEditBridge::Connection::messageReceived (const MemoryBlock& message)
{
    auto text = String::fromUTF8 ((const char*) message.getData(), (int) message.getSize());
    auto request = JSON::parse (text);

    if (! request.isObject())
    {
        sendError (0, -32700, "Request was not valid JSON.");
        return;
    }

    {
        const ScopedLock lock (pendingRequestsLock);
        pendingRequests.add (request);
    }

    triggerAsyncUpdate();
}

void LiveEditBridge::Connection::handleAsyncUpdate()
{
    Array<var> requests;

    {
        const ScopedLock lock (pendingRequestsLock);
        requests.swapWith (pendingRequests);
    }

    for (const auto& request : requests)
        bridge.handleRequest (*this, request);
}

void LiveEditBridge::handleRequest (Connection& connection, const var& request)
{
    auto* object = request.getDynamicObject();

    if (object == nullptr)
    {
        connection.sendError (0, -32600, "Request must be a JSON object.");
        return;
    }

    int requestId = 0;

    if (! extractRequestId (*object, requestId))
    {
        connection.sendError (0, -32600, "Request id must be an integer.");
        return;
    }

    String token;

    if (! extractToken (*object, token) || token != sessionInfo.token)
    {
        connection.sendError (requestId, -32001, "Invalid live-edit token.");
        return;
    }

    auto method = readStringProperty (*object, "method");

    if (method.isEmpty())
    {
        connection.sendError (requestId, -32600, "Missing method.");
        return;
    }

    const auto documentFile = extractDocumentFile (*object);
    const auto projectFile = extractProjectFile (*object);

    if (method == "project.inspect")
    {
        handleProjectInspect (connection, requestId, projectFile);
        return;
    }

    if (method == "document.open")
    {
        handleDocumentOpen (connection, requestId, documentFile, projectFile);
        return;
    }

    if (method == "document.inspect")
    {
        handleInspect (connection, requestId, *object, documentFile);
        return;
    }

    if (method == "document.capture")
    {
        handleCapture (connection, requestId, documentFile);
        return;
    }

    if (method == "component.catalog")
    {
        handleComponentCatalog (connection, requestId, documentFile);
        return;
    }

    if (method == "edit.previewComponents")
    {
        handlePreviewComponents (connection, requestId, *object, documentFile);
        return;
    }

    if (method == "edit.previewSlider")
    {
        handlePreviewSlider (connection, requestId, *object, documentFile);
        return;
    }

    if (method == "edit.previewSliders")
    {
        handlePreviewSliders (connection, requestId, *object, documentFile);
        return;
    }

    if (method == "edit.apply")
    {
        handleApply (connection, requestId, documentFile);
        return;
    }

    if (method == "session.cancel")
    {
        handleCancel (connection, requestId, documentFile);
        return;
    }

    if (method == "session.status")
    {
        handleStatus (connection, requestId, documentFile);
        return;
    }

    connection.sendError (requestId, -32601, "Unknown method: " + method);
}

String LiveEditBridge::readStringProperty (const DynamicObject& object, const String& name, const String& defaultValue)
{
    return object.hasProperty (name) ? object.getProperty (name).toString() : defaultValue;
}

double LiveEditBridge::readDoubleProperty (const DynamicObject& object, const String& name, double defaultValue)
{
    return object.hasProperty (name) ? (double) object.getProperty (name) : defaultValue;
}

int LiveEditBridge::readIntProperty (const DynamicObject& object, const String& name, int defaultValue)
{
    return object.hasProperty (name) ? (int) object.getProperty (name) : defaultValue;
}

bool LiveEditBridge::extractRequestId (const DynamicObject& object, int& requestId)
{
    if (! object.hasProperty ("id"))
        return false;

    requestId = (int) object.getProperty ("id");
    return true;
}

bool LiveEditBridge::extractToken (const DynamicObject& object, String& token)
{
    if (object.hasProperty ("token"))
    {
        token = object.getProperty ("token").toString();
        return token.isNotEmpty();
    }

    if (auto* params = object.getProperty ("params").getDynamicObject())
    {
        token = params->getProperty ("token").toString();
        return token.isNotEmpty();
    }

    return false;
}

File LiveEditBridge::extractDocumentFile (const DynamicObject& object)
{
    if (object.hasProperty ("documentFile"))
        return File (object.getProperty ("documentFile").toString());

    if (auto* params = object.getProperty ("params").getDynamicObject())
    {
        if (params->hasProperty ("documentFile"))
            return File (params->getProperty ("documentFile").toString());
    }

    return {};
}

File LiveEditBridge::extractProjectFile (const DynamicObject& object)
{
    if (object.hasProperty ("projectFile"))
        return File (object.getProperty ("projectFile").toString());

    if (auto* params = object.getProperty ("params").getDynamicObject())
        if (params->hasProperty ("projectFile"))
            return File (params->getProperty ("projectFile").toString());

    return {};
}

namespace
{
    static MainWindow* findProjectWindow (ProjucerApplication& app, const File& projectFile)
    {
        if (projectFile != File{})
            return app.mainWindowList.getMainWindowForFile (projectFile);

        if (auto* project = app.mainWindowList.getFrontmostProject())
            return app.mainWindowList.getMainWindowForFile (project->getFile());

        return nullptr;
    }

    static void addGuiDocuments (Project::Item item, Array<var>& documents, const File& activeFile)
    {
        if (item.isFile())
        {
            const auto file = item.getFile();

            if (JucerDocument::isValidJucerCppFile (file))
            {
                auto metadata = JucerDocument::pullMetaDataFromCppFile (file.loadFileAsString());
                auto document = std::make_unique<DynamicObject>();
                document->setProperty ("id", item.getID());
                document->setProperty ("name", item.getName());
                document->setProperty ("className", metadata != nullptr ? metadata->getStringAttribute ("className") : String());
                document->setProperty ("documentType", metadata != nullptr ? metadata->getStringAttribute ("documentType", "Component") : String());
                document->setProperty ("cppFile", file.getFullPathName());
                document->setProperty ("headerFile", file.withFileExtension (".h").getFullPathName());
                document->setProperty ("isOpen", file == activeFile);
                documents.add (var (document.release()));
            }
        }

        for (int i = 0; i < item.getNumChildren(); ++i)
            addGuiDocuments (item.getChild (i), documents, activeFile);
    }
}

void LiveEditBridge::handleProjectInspect (Connection& connection, int id, const File& projectFile)
{
    auto* window = findProjectWindow (app, projectFile);
    auto* content = window != nullptr ? window->getProjectContentComponent() : nullptr;
    auto* project = content != nullptr ? content->getProject() : nullptr;

    if (project == nullptr)
    {
        connection.sendError (id, -32003, "Requested project is not open.");
        return;
    }

    Array<var> documents;
    addGuiDocuments (project->getMainGroup(), documents, content->getCurrentFile());

    auto result = std::make_unique<DynamicObject>();
    result->setProperty ("projectFile", project->getFile().getFullPathName());
    result->setProperty ("projectName", project->getProjectNameString());
    result->setProperty ("guiDocuments", var (documents));
    connection.sendResponse (makeResultResponse (id, var (result.release())));
}

void LiveEditBridge::handleDocumentOpen (Connection& connection, int id, const File& documentFile, const File& projectFile)
{
    auto* window = findProjectWindow (app, projectFile);
    auto* content = window != nullptr ? window->getProjectContentComponent() : nullptr;
    auto* project = content != nullptr ? content->getProject() : nullptr;

    if (project == nullptr)
    {
        connection.sendError (id, -32003, "Requested project is not open.");
        return;
    }

    if (! documentFile.existsAsFile()
        || ! project->getMainGroup().findItemForFile (documentFile).isValid()
        || ! JucerDocument::isValidJucerCppFile (documentFile))
    {
        connection.sendError (id, -32004, "Requested file is not a GUI Component in this project.");
        return;
    }

    window->toFront (true);

    if (! content->showEditorForFile (documentFile, true))
    {
        connection.sendError (id, -32005, "Could not open the requested GUI Component.");
        return;
    }

    auto result = std::make_unique<DynamicObject>();
    result->setProperty ("opened", true);
    result->setProperty ("projectFile", project->getFile().getFullPathName());
    result->setProperty ("documentFile", documentFile.getFullPathName());
    connection.sendResponse (makeResultResponse (id, var (result.release())));
}

JucerDocumentEditor* LiveEditBridge::findEditorInComponent (Component& component)
{
    if (auto* editor = dynamic_cast<JucerDocumentEditor*> (&component))
        return editor;

    for (int i = 0; i < component.getNumChildComponents(); ++i)
        if (auto* child = findEditorInComponent (*component.getChildComponent (i)))
            return child;

    return nullptr;
}

JucerDocumentEditor* LiveEditBridge::findDocumentEditor (const File& file) const
{
    if (auto* editor = findEditorInDesktop())
    {
        if (editor->getDocument() != nullptr && matchesDocumentFile (*editor->getDocument(), file))
            return editor;
    }

    for (auto* window : app.mainWindowList.windows)
    {
        if (window == nullptr)
            continue;

        if (auto* pcc = window->getProjectContentComponent())
        {
            if (file.existsAsFile() && pcc->getCurrentDocument() != nullptr)
            {
                if (auto* editor = dynamic_cast<JucerDocumentEditor*> (pcc->getEditorComponent()))
                    if (editor->getDocument() != nullptr && matchesDocumentFile (*editor->getDocument(), file))
                        return editor;
            }
        }
    }

    return nullptr;
}

ComponentLayoutPanel* LiveEditBridge::findLayoutPanel (const File& file) const
{
    if (auto* editor = findDocumentEditor (file))
        return editor->getCurrentLayoutPanel();

    if (file == File{})
        if (auto* editor = findEditorInDesktop())
            return editor->getCurrentLayoutPanel();

    return nullptr;
}

void LiveEditBridge::handleInspect (Connection& connection, int id, const DynamicObject&, const File& documentFile)
{
    auto* editor = findDocumentEditor (documentFile);

    if (editor == nullptr || editor->getDocument() == nullptr)
    {
        connection.sendError (id, -32002, "Requested document is not open.");
        return;
    }

    ProjucerAutomation::GuiDocumentAdapter adapter (*editor->getDocument());
    auto snapshot = adapter.createSnapshot();

    auto root = std::make_unique<DynamicObject>();
    root->setProperty ("projectFile", snapshot.projectFile.getFullPathName());
    root->setProperty ("guiFile", snapshot.guiFile.getFullPathName());

    auto bounds = std::make_unique<DynamicObject>();
    bounds->setProperty ("x", snapshot.componentBounds.getX());
    bounds->setProperty ("y", snapshot.componentBounds.getY());
    bounds->setProperty ("width", snapshot.componentBounds.getWidth());
    bounds->setProperty ("height", snapshot.componentBounds.getHeight());
    root->setProperty ("componentBounds", var (bounds.release()));

    auto grid = std::make_unique<DynamicObject>();
    grid->setProperty ("size", snapshot.snapGridSize);
    grid->setProperty ("active", snapshot.snapActive);
    grid->setProperty ("shown", snapshot.snapShown);
    root->setProperty ("grid", var (grid.release()));

    Array<var> components;

    for (const auto& component : snapshot.components)
    {
        auto item = std::make_unique<DynamicObject>();
        item->setProperty ("id", String::toHexString (component.id));
        item->setProperty ("type", component.type);
        item->setProperty ("name", component.name);
        item->setProperty ("memberName", component.memberName);

        auto componentBounds = std::make_unique<DynamicObject>();
        componentBounds->setProperty ("x", component.bounds.getX());
        componentBounds->setProperty ("y", component.bounds.getY());
        componentBounds->setProperty ("width", component.bounds.getWidth());
        componentBounds->setProperty ("height", component.bounds.getHeight());
        item->setProperty ("bounds", var (componentBounds.release()));

        auto properties = std::make_unique<DynamicObject>();
        for (int i = 0; i < component.properties.size(); ++i)
            properties->setProperty (component.properties.getName (i), component.properties.getValueAt (i));
        item->setProperty ("properties", var (properties.release()));

        if (component.slider.has_value())
        {
            const auto& source = *component.slider;
            auto slider = std::make_unique<DynamicObject>();
            slider->setProperty ("minimum", source.minimum);
            slider->setProperty ("maximum", source.maximum);
            slider->setProperty ("interval", source.interval);
            slider->setProperty ("skewFactor", source.skewFactor);
            slider->setProperty ("style", source.style);
            slider->setProperty ("textBoxPosition", source.textBoxPosition);
            slider->setProperty ("textBoxEditable", source.textBoxEditable);
            slider->setProperty ("textBoxWidth", source.textBoxWidth);
            slider->setProperty ("textBoxHeight", source.textBoxHeight);
            item->setProperty ("slider", var (slider.release()));
        }

        components.add (var (item.release()));
    }

    root->setProperty ("components", var (components));
    connection.sendResponse (makeResultResponse (id, var (root.release())));
}

void LiveEditBridge::handleCapture (Connection& connection, int id, const File& documentFile)
{
    auto* panel = findLayoutPanel (documentFile);

    if (panel == nullptr || panel->getWidth() <= 0 || panel->getHeight() <= 0)
    {
        connection.sendError (id, -32002, "Requested GUI Editor is not visible.");
        return;
    }

    const auto image = panel->Component::createComponentSnapshot (panel->getLocalBounds(), true, 1.0f);
    MemoryOutputStream encodedImage;
    PNGImageFormat png;

    if (! image.isValid() || ! png.writeImageToStream (image, encodedImage))
    {
        connection.sendError (id, -32006, "Could not capture the GUI Editor image.");
        return;
    }

    auto result = std::make_unique<DynamicObject>();
    result->setProperty ("mimeType", "image/png");
    result->setProperty ("data", Base64::toBase64 (encodedImage.getData(), encodedImage.getDataSize()));
    result->setProperty ("width", image.getWidth());
    result->setProperty ("height", image.getHeight());
    connection.sendResponse (makeResultResponse (id, var (result.release())));
}

void LiveEditBridge::handleComponentCatalog (Connection& connection, int id, const File& documentFile)
{
    auto* editor = findDocumentEditor (documentFile);

    if (editor == nullptr || editor->getDocument() == nullptr)
    {
        connection.sendError (id, -32002, "Requested document is not open.");
        return;
    }

    GuiDocumentAdapter adapter (*editor->getDocument());
    Array<var> types;

    for (const auto& descriptor : adapter.createComponentTypeCatalog())
    {
        auto type = std::make_unique<DynamicObject>();
        type->setProperty ("type", descriptor.type);
        type->setProperty ("xmlTag", descriptor.xmlTag);
        type->setProperty ("className", descriptor.className);

        auto size = std::make_unique<DynamicObject>();
        size->setProperty ("width", descriptor.defaultSize.x);
        size->setProperty ("height", descriptor.defaultSize.y);
        type->setProperty ("defaultSize", var (size.release()));

        auto properties = std::make_unique<DynamicObject>();
        for (int i = 0; i < descriptor.defaultProperties.size(); ++i)
            properties->setProperty (descriptor.defaultProperties.getName (i),
                                     descriptor.defaultProperties.getValueAt (i));
        type->setProperty ("defaultProperties", var (properties.release()));
        types.add (var (type.release()));
    }

    auto result = std::make_unique<DynamicObject>();
    result->setProperty ("types", var (types));
    connection.sendResponse (makeResultResponse (id, var (result.release())));
}

void LiveEditBridge::handlePreviewComponents (Connection& connection, int id,
                                              const DynamicObject& object, const File& documentFile)
{
    auto* panel = findLayoutPanel (documentFile);
    auto* editor = findDocumentEditor (documentFile);
    const auto* params = object.getProperty ("params").getDynamicObject();
    const auto* components = params != nullptr ? params->getProperty ("components").getArray() : nullptr;

    if (panel == nullptr || editor == nullptr || editor->getDocument() == nullptr)
    {
        connection.sendError (id, -32002, "Requested document is not open.");
        return;
    }

    if (components == nullptr || components->isEmpty())
    {
        connection.sendError (id, -32602, "params.components must contain at least one component.");
        return;
    }

    std::vector<ComponentDraft> drafts;
    drafts.reserve ((size_t) components->size());

    for (const auto& value : *components)
    {
        const auto* source = value.getDynamicObject();
        if (source == nullptr)
        {
            connection.sendError (id, -32602, "Each components entry must be an object.");
            return;
        }

        ComponentDraft draft;
        draft.type = readStringProperty (*source, "type");
        draft.name = readStringProperty (*source, "name");
        draft.memberName = readStringProperty (*source, "memberName");
        draft.placement.anchor = PlacementAnchor::componentTopLeft;
        draft.placement.offset = { readIntProperty (*source, "x"), readIntProperty (*source, "y") };
        draft.placement.size = { readIntProperty (*source, "width", 100), readIntProperty (*source, "height", 24) };

        if (const auto* properties = source->getProperty ("properties").getDynamicObject())
            for (const auto& property : properties->getProperties())
                draft.properties.set (property.name, property.value);

        drafts.push_back (std::move (draft));
    }

    GuiDocumentAdapter adapter (*editor->getDocument());
    for (const auto& draft : drafts)
        if (auto validation = adapter.validate (draft); validation.failed())
        {
            connection.sendError (id, -32602, validation.getErrorMessage());
            return;
        }

    panel->showLiveEditPreview (drafts);

    auto response = std::make_unique<DynamicObject>();
    response->setProperty ("previewing", components->size());
    connection.sendResponse (makeResultResponse (id, var (response.release())));
}

void LiveEditBridge::handlePreviewSlider (Connection& connection, int id, const DynamicObject& object, const File& documentFile)
{
    auto* editor = findDocumentEditor (documentFile);

    if (editor == nullptr || editor->getDocument() == nullptr)
    {
        connection.sendError (id, -32002, "Requested document is not open.");
        return;
    }

    const auto* params = object.getProperty ("params").getDynamicObject();

    if (params == nullptr)
    {
        connection.sendError (id, -32602, "Missing params.");
        return;
    }

    SliderDraft draft;
    draft.name = readStringProperty (*params, "name", "Lowpass Filter Slider");
    draft.memberName = readStringProperty (*params, "memberName", "lowpassFilterSlider");
    draft.minimum = readDoubleProperty (*params, "minimum", 20.0);
    draft.maximum = readDoubleProperty (*params, "maximum", 20000.0);
    draft.interval = readDoubleProperty (*params, "interval", 0.0);
    draft.placement.anchor = PlacementAnchor::componentCentre;
    draft.placement.offset.x = readIntProperty (*params, "offsetX", 0);
    draft.placement.offset.y = readIntProperty (*params, "offsetY", 0);
    draft.placement.size.x = readIntProperty (*params, "width", 120);
    draft.placement.size.y = readIntProperty (*params, "height", 120);
    draft.style = SliderStyle::rotaryHorizontalVerticalDrag;
    draft.textBoxPosition = SliderTextBoxPosition::none;

    if (auto* panel = editor->getCurrentLayoutPanel())
        panel->showLiveEditPreview (draft);

    auto response = std::make_unique<DynamicObject>();
    response->setProperty ("applied", true);
    connection.sendResponse (makeResultResponse (id, var (response.release())));
}

void LiveEditBridge::handlePreviewSliders (Connection& connection, int id, const DynamicObject& object, const File& documentFile)
{
    auto* editor = findDocumentEditor (documentFile);

    if (editor == nullptr || editor->getDocument() == nullptr)
    {
        connection.sendError (id, -32002, "Requested document is not open.");
        return;
    }

    const auto* params = object.getProperty ("params").getDynamicObject();
    const auto* sliders = params != nullptr ? params->getProperty ("sliders").getArray() : nullptr;

    if (sliders == nullptr || sliders->isEmpty())
    {
        connection.sendError (id, -32602, "params.sliders must contain at least one Slider.");
        return;
    }

    std::vector<SliderDraft> drafts;
    drafts.reserve ((size_t) sliders->size());

    for (const auto& value : *sliders)
    {
        const auto* slider = value.getDynamicObject();

        if (slider == nullptr)
        {
            connection.sendError (id, -32602, "Each sliders entry must be an object.");
            return;
        }

        SliderDraft draft;
        draft.name = readStringProperty (*slider, "name");
        draft.memberName = readStringProperty (*slider, "memberName");
        draft.minimum = readDoubleProperty (*slider, "minimum", 0.0);
        draft.maximum = readDoubleProperty (*slider, "maximum", 1.0);
        draft.interval = readDoubleProperty (*slider, "interval", 0.0);
        draft.placement.anchor = PlacementAnchor::componentTopLeft;
        draft.placement.offset.x = readIntProperty (*slider, "x", 0);
        draft.placement.offset.y = readIntProperty (*slider, "y", 0);
        draft.placement.size.x = readIntProperty (*slider, "width", 120);
        draft.placement.size.y = readIntProperty (*slider, "height", 120);
        drafts.push_back (std::move (draft));
    }

    if (auto* panel = editor->getCurrentLayoutPanel())
        panel->showLiveEditPreview (drafts);

    auto response = std::make_unique<DynamicObject>();
    response->setProperty ("previewing", sliders->size());
    connection.sendResponse (makeResultResponse (id, var (response.release())));
}

void LiveEditBridge::handleApply (Connection& connection, int id, const File& documentFile)
{
    auto* panel = findLayoutPanel (documentFile);

    if (panel == nullptr)
    {
        connection.sendError (id, -32002, "Requested document is not open.");
        return;
    }

    panel->applyLiveEditPreview();

    auto response = std::make_unique<DynamicObject>();
    response->setProperty ("applied", true);
    connection.sendResponse (makeResultResponse (id, var (response.release())));
}

void LiveEditBridge::handleCancel (Connection& connection, int id, const File& documentFile)
{
    auto* panel = findLayoutPanel (documentFile);

    if (panel == nullptr)
    {
        connection.sendError (id, -32002, "Requested document is not open.");
        return;
    }

    const auto cancelled = panel->cancelLiveEditPreview ("client_cancel");

    auto response = std::make_unique<DynamicObject>();
    response->setProperty ("cancelled", cancelled);
    connection.sendResponse (makeResultResponse (id, var (response.release())));
}

void LiveEditBridge::handleStatus (Connection& connection, int id, const File& documentFile)
{
    auto* panel = findLayoutPanel (documentFile);

    if (panel == nullptr)
    {
        connection.sendError (id, -32002, "Requested document is not open.");
        return;
    }

    auto response = std::make_unique<DynamicObject>();
    response->setProperty ("state", panel->getLiveEditState());
    response->setProperty ("reason", panel->getLiveEditCompletionReason());
    response->setProperty ("previewVisible", panel->isLiveEditPreviewVisible());
    response->setProperty ("paused", panel->isLiveEditPaused());
    response->setProperty ("applied", panel->isLiveEditApplied());
    connection.sendResponse (makeResultResponse (id, var (response.release())));
}
} // namespace ProjucerAutomation
