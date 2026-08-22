#pragma once

#include "jucer_AiSession.h"
#include "jucer_CodexAuth.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <atomic>
#include <functional>
#include <memory>

/* Displays the AI chat for one project. */
class AiChatView final : public juce::Component,
                         private juce::ChangeListener,
                         private juce::KeyListener
{
public:
    AiChatView (std::shared_ptr<AiSession> sessionToUse,
                std::shared_ptr<CodexAuth> authToUse);
    ~AiChatView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    class SignInWorker;
    class RoundIconButton;
    class FlatButton;
    class ChatHistoryView;
    class DiffPreviewView;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    /*  Enter は送信。改行は Shift+Enter と Ctrl+J。 */
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;

    /*  サインインの経路は二つある。ブラウザ経路が既定で、前提条件が無い。
        デバイスコード経路は ChatGPT のセキュリティ設定で有効にしたアカウントでしか
        使えないため、代替として置いてある。 */
    enum class SignInMethod { browser, deviceCode };

    void startSignIn (SignInMethod method);
    void cancelSignIn();
    void stopSignInWorker();
    void rebuildHistory();
    void updateHistoryLayout (bool forceFollow);
    void scrollHistoryToBottom();
    bool isHistoryNearBottom() const;
    void updateVisibility();
    void sendCurrentInput();
    bool handleSlashCommand (const juce::String& text);
    void showModelPicker();
    void updateModelButton();
    void updatePermissionButton();
    void showPermissionMenu();
    void updateExecTargetButton();
    void chooseFilesToMention();
    juce::File projectRootForChooser() const;

    /*  メニュー項目 ID の区画。0 は「選択なし」なので 1 以降を使う。 */
    static constexpr int controlRowHeight = 30;
    static constexpr int roundButtonSize  = 26;

    static constexpr int resetId      = 1;
    static constexpr int modelBaseId  = 100;
    static constexpr int effortBaseId = 200;
    static constexpr int speedBaseId  = 300;

    std::shared_ptr<AiSession> session;
    std::shared_ptr<CodexAuth> auth;

    juce::Component signInCard;
    juce::Label signInTitle;
    juce::Label signInCode;
    juce::Label signInInstructions;
    /*  入力欄の下に置く、現在のモデル設定を示すボタン。押すと Codex デスクトップ版と
        同じ「モデル / 推論レベル / 速度」のメニューが出る。 */
    /*  入力欄の下の操作列。ChatGPT の作りに合わせる。
        左から「ファイル追加(+)」「承認の扱い」、右へ寄せて「モデル設定」「送信/停止」。 */
    std::unique_ptr<FlatButton> modelButton;
    std::unique_ptr<FlatButton> permissionButton;
    std::unique_ptr<FlatButton> execTargetButton;
    std::unique_ptr<RoundIconButton> addFileButton;
    std::unique_ptr<RoundIconButton> sendStopButton;

    juce::TextButton signInButton { "Sign in with ChatGPT" };
    juce::TextButton deviceCodeButton { "Use the other sign-in method" };
    juce::TextButton copyCodeButton { "Copy code" };
    juce::TextButton openBrowserButton { "Open browser" };
    juce::TextButton cancelSignInButton { "Cancel" };

    std::shared_ptr<std::atomic<bool>> lifetimeToken;
    std::shared_ptr<std::atomic<bool>> signInStopToken;
    std::unique_ptr<SignInWorker> signInWorker;
    juce::String verificationUrl;

    std::unique_ptr<ChatHistoryView> historyContent;
    juce::Viewport historyViewport;

    juce::Component approvalCard;
    juce::Label approvalTitle;
    std::unique_ptr<DiffPreviewView> approvalDiffContent;
    juce::Viewport approvalDiffViewport;
    juce::TextButton approveButton { "Apply" };
    juce::TextButton runInTerminalButton { "Run in terminal" };
    juce::TextButton rejectButton { "Reject" };
    juce::ToggleButton autoApproveToggle { "Auto-approve changes" };

    juce::TextEditor input;

    /*  入力欄と履歴を切り分ける帯。Projucer の地色で塗る。 */
    juce::Rectangle<int> separatorArea;

    /*  + で選んだファイル。画像はそのまま本文に載せて送る。 */
    juce::Array<juce::File> pendingAttachments;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AiChatView)
};
