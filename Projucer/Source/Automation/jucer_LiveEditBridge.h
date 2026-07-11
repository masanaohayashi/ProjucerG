/*
  ==============================================================================

   This file is part of the JUCE framework.
   Copyright (c) Raw Material Software Limited

   See LICENSE.md for the terms that apply to this repository.

  ==============================================================================
*/

#pragma once

#include "jucer_AutomationTypes.h"

class ProjucerApplication;
class JucerDocumentEditor;
class ComponentLayoutPanel;

namespace ProjucerAutomation
{
struct LiveEditSessionInfo
{
    int port = -1;
    String token;
};

File getLiveEditSessionInfoFile();
bool readLiveEditSessionInfo (LiveEditSessionInfo&);

class LiveEditBridge final : private InterprocessConnectionServer
{
public:
    explicit LiveEditBridge (ProjucerApplication&);
    ~LiveEditBridge() override;

    void start();
    void stop();

private:
    class Connection final : public InterprocessConnection,
                             private AsyncUpdater
    {
    public:
        explicit Connection (LiveEditBridge&);
        ~Connection() override;

        void connectionMade() override;
        void connectionLost() override;
        void messageReceived (const MemoryBlock&) override;

        void sendResponse (const var&);
        void sendError (int id, int code, const String& message);

    private:
        void handleAsyncUpdate() override;

        LiveEditBridge& bridge;
        CriticalSection pendingRequestsLock;
        Array<var> pendingRequests;
    };

    InterprocessConnection* createConnectionObject() override;

    void handleRequest (Connection&, const var&);
    void handleProjectInspect (Connection&, int id, const File& projectFile);
    void handleDocumentOpen (Connection&, int id, const File& documentFile, const File& projectFile);
    void handleInspect (Connection&, int id, const DynamicObject&, const File& documentFile);
    void handlePreviewSlider (Connection&, int id, const DynamicObject&, const File& documentFile);
    void handleApply (Connection&, int id, const File& documentFile);
    void handleCancel (Connection&, int id, const File& documentFile);

    static String readStringProperty (const DynamicObject&, const String& name, const String& defaultValue = {});
    static double readDoubleProperty (const DynamicObject&, const String& name, double defaultValue = 0.0);
    static int readIntProperty (const DynamicObject&, const String& name, int defaultValue = 0);

    static bool extractRequestId (const DynamicObject&, int& requestId);
    static bool extractToken (const DynamicObject&, String& token);
    static File extractDocumentFile (const DynamicObject&);
    static File extractProjectFile (const DynamicObject&);

    JucerDocumentEditor* findDocumentEditor (const File&) const;
    ComponentLayoutPanel* findLayoutPanel (const File&) const;
    static JucerDocumentEditor* findEditorInComponent (Component&);

    void writeSessionInfo();
    void deleteSessionInfo();
    String createToken();

    ProjucerApplication& app;
    LiveEditSessionInfo sessionInfo;
    File sessionInfoFile;
    bool running = false;
};
} // namespace ProjucerAutomation
