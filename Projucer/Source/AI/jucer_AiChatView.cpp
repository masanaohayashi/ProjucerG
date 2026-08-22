#include "../Application/jucer_Headers.h"

#include "jucer_AiChatView.h"

#include "jucer_AiModels.h"

#include <algorithm>
#include <cmath>
#include <utility>
#include <vector>

namespace
{
    constexpr int rowHeight = 34;

    /*  チャットの表示はターミナルと揃える。同じアプリの中で、同じ「機械の出力を
        読む場所」なので、フォントと配色が違うと落ち着かない。
        値は Source/Terminal/jucer_TerminalTranslation.h の既定色。 */
    const juce::Colour chatBackground { 0xff000000 };
    const juce::Colour chatForeground { 0xffc0c0c0 };

    /*  自分の発言は角丸のバブルにして、AI の出力と見分けられるようにする。
        地色に沈めてしまうと、どこからが自分の入力か分からなくなる。 */
    const juce::Colour userBubble     { 0xff303030 };
    const juce::Colour userText       { 0xffececec };
    const juce::Colour inputSurface   { 0xff1a1a1a };

    juce::Font getChatFont()
    {
        return getAppSettings().appearance.getCodeFont();
    }
    constexpr int inputHeight = 80;
    constexpr int padding = 10;
    constexpr int maximumErrorLength = 512;

    juce::String prefixFor (AiSession::Entry::Kind kind)
    {
        switch (kind)
        {
            case AiSession::Entry::Kind::user:      return "You: ";
            case AiSession::Entry::Kind::assistant: return "AI: ";
            case AiSession::Entry::Kind::tool:      return "Tool: ";
            case AiSession::Entry::Kind::error:     return "Error: ";
        }

        return {};
    }

    juce::String safeErrorMessage (juce::String message, const juce::String& fallback)
    {
        message = message.trim();

        if (message.isEmpty())
            return fallback;

        const auto lower = message.toLowerCase();

        if (lower.contains ("access_token")
            || lower.contains ("refresh_token")
            || lower.contains ("id_token")
            || lower.contains ("authorization: bearer")
            || lower.contains ("bearer "))
        {
            return fallback;
        }

        if (message.length() > maximumErrorLength)
            message = message.substring (0, maximumErrorLength) + "...";

        return message;
    }

    template <typename Fn>
    void callOnLiveView (const std::shared_ptr<std::atomic<bool>>& lifetime,
                         juce::Component::SafePointer<AiChatView> view,
                         Fn&& fn)
    {
        juce::MessageManager::callAsync ([lifetime, view, fn = std::forward<Fn> (fn)]() mutable
        {
            if (! lifetime->load (std::memory_order_acquire))
                return;

            if (auto* liveView = view.getComponent())
                fn (*liveView);
        });
    }
}

/*  丸い図形ボタン。送信・停止・ファイル追加に使う。

    送信と停止は同じ場所に出す。押す位置が変わらない方が、止めたいときに
    迷わない。ChatGPT や Codex デスクトップ版と同じ作り。
*/
class AiChatView::RoundIconButton final : public juce::Button
{
public:
    enum class Icon { send, stop, plus };

    RoundIconButton (const juce::String& name, Icon initialIcon)
        : juce::Button (name), icon (initialIcon)
    {
    }

    void setIcon (Icon newIcon)
    {
        if (icon == newIcon)
            return;

        icon = newIcon;
        repaint();
    }

    Icon getIcon() const noexcept    { return icon; }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
        const auto enabled = isEnabled();

        auto fill = icon == Icon::plus ? juce::Colours::transparentBlack
                                       : chatForeground.withAlpha (enabled ? 0.9f : 0.3f);

        if (down)       fill = fill.darker (0.2f);
        else if (highlighted) fill = fill.brighter (0.15f);

        if (icon != Icon::plus)
        {
            g.setColour (fill);
            g.fillEllipse (bounds);
        }
        else if (highlighted || down)
        {
            g.setColour (chatForeground.withAlpha (0.15f));
            g.fillEllipse (bounds);
        }

        const auto glyph = icon == Icon::plus ? chatForeground.withAlpha (enabled ? 0.9f : 0.3f)
                                              : chatBackground;
        g.setColour (glyph);

        const auto centre = bounds.getCentre();
        const auto size = bounds.getWidth() * 0.34f;

        switch (icon)
        {
            case Icon::send:
            {
                // 上向きの矢印。
                juce::Path arrow;
                arrow.startNewSubPath (centre.x, centre.y + size);
                arrow.lineTo (centre.x, centre.y - size);
                arrow.startNewSubPath (centre.x - size * 0.72f, centre.y - size * 0.28f);
                arrow.lineTo (centre.x, centre.y - size);
                arrow.lineTo (centre.x + size * 0.72f, centre.y - size * 0.28f);
                g.strokePath (arrow, juce::PathStrokeType (juce::jmax (1.6f, size * 0.36f),
                                                           juce::PathStrokeType::curved,
                                                           juce::PathStrokeType::rounded));
                break;
            }

            case Icon::stop:
            {
                // 停止を示す四角。
                const auto side = size * 1.15f;
                g.fillRoundedRectangle (juce::Rectangle<float> (side, side).withCentre (centre), 2.0f);
                break;
            }

            case Icon::plus:
            {
                juce::Path plus;
                plus.startNewSubPath (centre.x - size, centre.y);
                plus.lineTo (centre.x + size, centre.y);
                plus.startNewSubPath (centre.x, centre.y - size);
                plus.lineTo (centre.x, centre.y + size);
                g.strokePath (plus, juce::PathStrokeType (juce::jmax (1.4f, size * 0.28f),
                                                          juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded));
                break;
            }
        }
    }

private:
    Icon icon;
};

/*  枠も塗りも持たない文字ボタン。モードやモデルの表示に使う。

    ここは主役ではないので、塗りつぶしのボタンにすると場所を取りすぎるうえ、
    押すものが多く見えて散らかる。
*/
class AiChatView::FlatButton final : public juce::Button
{
public:
    explicit FlatButton (const juce::String& name) : juce::Button (name) {}

    /** 先頭に出す小さな記号。空なら描かない。 */
    void setLeadingGlyph (const juce::String& glyph)
    {
        leadingGlyph = glyph;
        repaint();
    }

    /** 末尾に下向きの山括弧を出すか。 */
    void setShowsChevron (bool shouldShow)
    {
        showsChevron = shouldShow;
        repaint();
    }

    /** 主文のあとに続ける淡い補足。 */
    void setDetailText (const juce::String& text)
    {
        detail = text;
        repaint();
    }

    juce::String getDetailText() const    { return detail; }

    int getPreferredWidth() const
    {
        const auto font = getChatFont();
        auto width = juce::GlyphArrangement::getStringWidthInt (font, getButtonText());

        if (detail.isNotEmpty())
            width += juce::GlyphArrangement::getStringWidthInt (font, "  " + detail);

        if (leadingGlyph.isNotEmpty())
            width += juce::GlyphArrangement::getStringWidthInt (font, leadingGlyph + " ");

        return width + (showsChevron ? 18 : 0) + 12;
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        if (highlighted || down)
        {
            g.setColour (chatForeground.withAlpha (down ? 0.16f : 0.10f));
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
        }

        const auto font = getChatFont();
        g.setFont (font);

        auto area = getLocalBounds().reduced (6, 0);

        if (showsChevron)
        {
            auto chevronArea = area.removeFromRight (16).toFloat();
            const auto centre = chevronArea.getCentre();

            juce::Path chevron;
            chevron.startNewSubPath (centre.x - 4.0f, centre.y - 2.0f);
            chevron.lineTo (centre.x, centre.y + 2.5f);
            chevron.lineTo (centre.x + 4.0f, centre.y - 2.0f);

            g.setColour (chatForeground.withAlpha (0.55f));
            g.strokePath (chevron, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved,
                                                         juce::PathStrokeType::rounded));
        }

        if (leadingGlyph.isNotEmpty())
        {
            const auto glyphWidth = juce::GlyphArrangement::getStringWidthInt (font, leadingGlyph + " ");
            g.setColour (chatForeground.withAlpha (0.55f));
            g.drawText (leadingGlyph, area.removeFromLeft (glyphWidth),
                        juce::Justification::centredLeft, false);
        }

        g.setColour (chatForeground.withAlpha (isEnabled() ? 0.85f : 0.35f));
        const auto mainWidth = juce::GlyphArrangement::getStringWidthInt (font, getButtonText());
        g.drawText (getButtonText(), area.removeFromLeft (mainWidth),
                    juce::Justification::centredLeft, false);

        if (detail.isNotEmpty())
        {
            g.setColour (chatForeground.withAlpha (0.5f));
            g.drawText ("  " + detail, area, juce::Justification::centredLeft, false);
        }
    }

private:
    juce::String leadingGlyph, detail;
    bool showsChevron = false;
};

class AiChatView::ChatHistoryView final : public juce::Component
{
public:
    explicit ChatHistoryView (std::function<void()> heightChangedToUse)
        : heightChanged (std::move (heightChangedToUse))
    {
        setOpaque (true);
    }

    void setEntries (const juce::Array<AiSession::Entry>& entriesToUse)
    {
        entries = entriesToUse;

        while (expanded.size() < entries.size())
            expanded.add (false);

        while (expanded.size() > entries.size())
            expanded.removeLast();

        repaint();
    }

    int getContentHeight() const
    {
        auto height = 8;

        for (int i = 0; i < entries.size(); ++i)
        {
            const auto& entry = entries.getReference (i);
            height += entry.kind == AiSession::Entry::Kind::tool ? toolHeaderHeight
                                                                  : textHeight (entry.text);

            if (entry.kind == AiSession::Entry::Kind::tool && expanded[i])
                height += textHeight (entry.text) + 4;

            height += 6;
        }

        return height + 8;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (chatBackground);
        g.setFont (getChatFont());

        auto y = 8;
        const auto width = juce::jmax (1, getWidth() - 16);

        for (int i = 0; i < entries.size(); ++i)
        {
            const auto& entry = entries.getReference (i);

            if (entry.kind == AiSession::Entry::Kind::tool)
            {
                const juce::Rectangle<int> header (8, y, width, toolHeaderHeight);
                g.setColour (juce::Colours::orange.withAlpha (0.16f));
                g.fillRoundedRectangle (header.toFloat(), 5.0f);
                g.setColour (juce::Colours::orange);
                // 非 ASCII のリテラルを juce::String へ直接渡すと CharPointer_ASCII として
                // 解釈され、アサートに掛かるうえ文字化けする。UTF-8 と明示して渡す。
                const auto marker = juce::String (juce::CharPointer_UTF8 (expanded[i] ? "\xe2\x96\xbe " : "\xe2\x96\xb8 "));

                g.drawText (marker + entry.text,
                            header.reduced (8, 0), juce::Justification::centredLeft, true);
                y += toolHeaderHeight;

                if (expanded[i])
                {
                    g.setColour (chatForeground);
                    g.drawFittedText (entry.text,
                                      juce::Rectangle<int> (16, y + 2, width - 16,
                                                            textHeight (entry.text)),
                                      juce::Justification::topLeft, 40);
                    y += textHeight (entry.text) + 4;
                }
            }
            else if (entry.kind == AiSession::Entry::Kind::user)
            {
                /*  自分の発言は右寄せのバブル。ChatGPT と同じ作りで、
                    ひと目で自分が書いた行だと分かるようにする。 */
                const auto height = textHeight (entry.text);
                const auto bubbleWidth = juce::jmin (width - 40,
                                                     juce::jmax (80, width * 3 / 4));
                const juce::Rectangle<int> bubble (8 + width - bubbleWidth, y, bubbleWidth, height);

                g.setColour (userBubble);
                g.fillRoundedRectangle (bubble.toFloat(), 10.0f);

                g.setColour (userText);
                g.drawFittedText (entry.text, bubble.reduced (10, 4),
                                  juce::Justification::topLeft, 40);
                y += height;
            }
            else
            {
                const auto colour = entry.kind == AiSession::Entry::Kind::error
                                         ? juce::Colours::indianred
                                         : chatForeground;
                g.setColour (colour);
                g.drawFittedText (entry.text,
                                  juce::Rectangle<int> (8, y, width, textHeight (entry.text)),
                                  juce::Justification::topLeft, 40);
                y += textHeight (entry.text);
            }

            y += 6;
        }
    }

    void mouseUp (const juce::MouseEvent& event) override
    {
        auto y = 8;

        for (int i = 0; i < entries.size(); ++i)
        {
            const auto& entry = entries.getReference (i);

            if (entry.kind == AiSession::Entry::Kind::tool)
            {
                if (juce::Rectangle<int> (8, y, juce::jmax (1, getWidth() - 16),
                                          toolHeaderHeight).contains (event.getPosition()))
                {
                    expanded.set (i, ! expanded[i]);
                    if (heightChanged != nullptr)
                        heightChanged();
                    repaint();
                    return;
                }

                y += toolHeaderHeight;
                if (expanded[i])
                    y += textHeight (entry.text) + 4;
            }
            else
            {
                y += textHeight (entry.text);
            }

            y += 6;
        }
    }

private:
    static constexpr int toolHeaderHeight = 30;

    static int textHeight (const juce::String& text)
    {
        auto lines = 1;
        for (auto position = text.indexOfChar ('\n'); position >= 0;
             position = text.indexOfChar (position + 1, '\n'))
            ++lines;
        const auto wrappedLines = juce::jmax (0, text.length() / 84);
        return juce::jlimit (24, 360, (lines + wrappedLines) * 18 + 6);
    }

    juce::Array<AiSession::Entry> entries;
    juce::Array<bool> expanded;
    std::function<void()> heightChanged;
};

class AiChatView::DiffPreviewView final : public juce::Component
{
public:
    void setText (const juce::String& textToUse)
    {
        lines.clear();
        lines.addLines (textToUse.replace ("\r", {}));
        setSize (getWidth(), getContentHeight());
        repaint();
    }

    int getContentHeight() const noexcept
    {
        return juce::jmax (lineHeight + 8, lines.size() * lineHeight + 8);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (chatBackground);
        g.setFont (getChatFont());

        for (int i = 0; i < lines.size(); ++i)
        {
            const auto& line = lines[i];
            auto colour = findColour (juce::Label::textColourId);

            if (line.startsWith ("+") && ! line.startsWith ("+++"))
            {
                g.setColour (juce::Colours::green.withAlpha (0.18f));
                g.fillRect (0, i * lineHeight, getWidth(), lineHeight);
                colour = juce::Colours::lightgreen;
            }
            else if (line.startsWith ("-") && ! line.startsWith ("---"))
            {
                g.setColour (juce::Colours::red.withAlpha (0.18f));
                g.fillRect (0, i * lineHeight, getWidth(), lineHeight);
                colour = juce::Colours::lightpink;
            }
            else if (line.startsWith ("@@"))
            {
                colour = juce::Colours::lightskyblue;
            }

            g.setColour (colour);
            g.setFont (juce::FontOptions { juce::Font::getDefaultMonospacedFontName(), 13.0f, 0 });
            g.drawText (line, 8, i * lineHeight, juce::jmax (1, getWidth() - 16), lineHeight,
                        juce::Justification::centredLeft, false);
        }
    }

private:
    static constexpr int lineHeight = 20;
    juce::StringArray lines;
};

class AiChatView::SignInWorker final : public juce::Thread
{
public:
    SignInWorker (std::shared_ptr<CodexAuth> authToUse,
                  const std::shared_ptr<std::atomic<bool>>& stopTokenToUse,
                  const std::shared_ptr<std::atomic<bool>>& lifetimeToUse,
                  juce::Component::SafePointer<AiChatView> viewToUse,
                  SignInMethod methodToUse)
        : juce::Thread ("AI Sign-In"),
          auth (std::move (authToUse)),
          stopToken (stopTokenToUse),
          lifetimeToken (lifetimeToUse),
          view (std::move (viewToUse)),
          method (methodToUse)
    {
    }

    ~SignInWorker() override
    {
        jassert (! isThreadRunning());
    }

    void requestStop()
    {
        stopToken->store (true, std::memory_order_release);
        signalThreadShouldExit();
        auth->cancelActiveRequest();
    }

    static void reapFinishedWorkers()
    {
        auto& workers = timedOutWorkers();
        workers.erase (std::remove_if (workers.begin(), workers.end(), [] (const auto& worker)
        {
            return ! worker->isThreadRunning();
        }), workers.end());
    }

    static bool hasRunningRetainedWorker()
    {
        reapFinishedWorkers();
        return ! timedOutWorkers().empty();
    }

    static void retain (std::unique_ptr<SignInWorker> worker)
    {
        timedOutWorkers().push_back (std::move (worker));
    }

    static std::vector<std::unique_ptr<SignInWorker>>& timedOutWorkers()
    {
        // Retained workers may still be running when the process shuts down.
        // Intentionally keep this owner alive until process termination so its
        // JUCE Thread objects are never destroyed while their threads run.
        static auto* workers = new std::vector<std::unique_ptr<SignInWorker>>;
        return *workers;
    }

    void run() override
    {
        if (method == SignInMethod::browser)
            runBrowserSignIn();
        else
            runDeviceCodeSignIn();
    }

    /*  既定の経路。認可ページを出し、ループバックで認可コードを受け取る。
        コード表示の UI は使わないので出さない。 */
    void runBrowserSignIn()
    {
        callOnLiveView (lifetimeToken, view, [] (AiChatView& liveView)
        {
            liveView.signInInstructions.setText ("Waiting for you to finish signing in...",
                                                 juce::dontSendNotification);
            liveView.cancelSignInButton.setVisible (true);
            liveView.cancelSignInButton.setEnabled (true);
            liveView.resized();
        });

        juce::String error;
        const auto signedIn = auth->signInWithBrowser (*stopToken, error);

        if (! lifetimeToken->load (std::memory_order_acquire))
            return;

        if (signedIn)
        {
            callOnLiveView (lifetimeToken, view, [] (AiChatView& liveView)
            {
                liveView.signInInstructions.setText ("Signed in successfully.",
                                                     juce::dontSendNotification);
                liveView.updateVisibility();
            });
            return;
        }

        const auto displayError = safeErrorMessage (
            error,
            stopToken->load (std::memory_order_acquire)
                ? "Sign-in was cancelled."
                : "Sign-in failed. Please try again.");

        callOnLiveView (lifetimeToken, view, [displayError] (AiChatView& liveView)
        {
            liveView.signInInstructions.setText (displayError, juce::dontSendNotification);
            liveView.signInButton.setEnabled (true);
            liveView.deviceCodeButton.setVisible (true);
            liveView.cancelSignInButton.setVisible (false);
            liveView.resized();
        });
    }

    void runDeviceCodeSignIn()
    {
        juce::String error;
        const auto code = auth->requestDeviceCode (error, stopToken.get());

        if (! lifetimeToken->load (std::memory_order_acquire))
            return;

        if (stopToken->load (std::memory_order_acquire))
        {
            callOnLiveView (lifetimeToken, view, [] (AiChatView& liveView)
            {
                liveView.signInInstructions.setText ("Sign-in was cancelled.",
                                                     juce::dontSendNotification);
                liveView.signInButton.setEnabled (true);
                liveView.cancelSignInButton.setVisible (false);
                liveView.openBrowserButton.setVisible (false);
                liveView.resized();
            });
            return;
        }

        if (! code.has_value())
        {
            const auto displayError = safeErrorMessage (error,
                                                         "Could not start sign-in. Please try again.");
            callOnLiveView (lifetimeToken, view, [displayError] (AiChatView& liveView)
            {
                liveView.signInInstructions.setText (displayError,
                                                     juce::dontSendNotification);
                liveView.signInButton.setEnabled (true);
                liveView.resized();
            });
            return;
        }

        const auto deviceCode = *code;

        DBG ("[AI][auth] device code ready: url=" << deviceCode.verificationUrl
             << "  code length=" << deviceCode.userCode.length()
             << "  interval=" << deviceCode.intervalSeconds << "s");

        callOnLiveView (lifetimeToken, view, [deviceCode] (AiChatView& liveView)
        {
            liveView.verificationUrl = deviceCode.verificationUrl;
            liveView.signInCode.setText (deviceCode.userCode, juce::dontSendNotification);
            /*  Codex CLI (login/src/device_code_auth.rs の device_code_prompt) と
                同じ手順を、同じ順序で示す。「まずアカウントにサインインする」を
                省くと、未サインインのブラウザではパスキー登録などの画面へ流れて
                しまい、コード入力欄に辿り着けない。 */
            liveView.signInInstructions.setText (
                "1. Open " + deviceCode.verificationUrl + " and sign in to your account.\n"
                "2. Enter the one-time code above (expires in 15 minutes).",
                juce::dontSendNotification);
            liveView.copyCodeButton.setVisible (true);
            liveView.openBrowserButton.setVisible (true);
            liveView.openBrowserButton.setEnabled (true);
            liveView.cancelSignInButton.setVisible (true);
            liveView.cancelSignInButton.setEnabled (true);
            liveView.resized();
        });

        const auto signedIn = auth->pollForTokens (deviceCode, *stopToken, error);

        if (! lifetimeToken->load (std::memory_order_acquire))
            return;

        if (signedIn)
        {
            callOnLiveView (lifetimeToken, view, [] (AiChatView& liveView)
            {
                liveView.signInInstructions.setText ("Signed in successfully.",
                                                     juce::dontSendNotification);
                liveView.updateVisibility();
            });
            return;
        }

        const auto displayError = safeErrorMessage (
            error,
            stopToken->load (std::memory_order_acquire)
                ? "Sign-in was cancelled."
                : "Sign-in failed. Please try again.");

        callOnLiveView (lifetimeToken, view, [displayError] (AiChatView& liveView)
        {
            liveView.signInCode.setText ({}, juce::dontSendNotification);
            liveView.verificationUrl.clear();
            liveView.signInInstructions.setText (displayError,
                                                 juce::dontSendNotification);
            liveView.signInButton.setEnabled (true);
            liveView.copyCodeButton.setVisible (false);
            liveView.openBrowserButton.setVisible (false);
            liveView.cancelSignInButton.setVisible (false);
            liveView.cancelSignInButton.setEnabled (true);
            liveView.openBrowserButton.setEnabled (true);
            liveView.resized();
        });
    }

private:
    std::shared_ptr<CodexAuth> auth;
    std::shared_ptr<std::atomic<bool>> stopToken;
    std::shared_ptr<std::atomic<bool>> lifetimeToken;
    juce::Component::SafePointer<AiChatView> view;
    SignInMethod method = SignInMethod::browser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SignInWorker)
};

//==============================================================================
AiChatView::AiChatView (std::shared_ptr<AiSession> sessionToUse,
                        std::shared_ptr<CodexAuth> authToUse)
    : session (std::move (sessionToUse)),
      auth (std::move (authToUse)),
      lifetimeToken (std::make_shared<std::atomic<bool>> (true))
{
    setTitle ("AI Chat");
    setOpaque (false);

    signInTitle.setText ("Sign in to ChatGPT", juce::dontSendNotification);
    signInTitle.setJustificationType (juce::Justification::centred);
    signInTitle.setFont (juce::FontOptions { 18.0f, juce::Font::bold });
    signInCard.addAndMakeVisible (signInTitle);

    signInCode.setJustificationType (juce::Justification::centred);
    signInCode.setFont (juce::FontOptions { 28.0f, juce::Font::bold });
    signInCard.addAndMakeVisible (signInCode);

    signInInstructions.setJustificationType (juce::Justification::centred);
    signInInstructions.setMinimumHorizontalScale (0.8f);
    signInInstructions.setText ("Sign in with your ChatGPT account to use the assistant.",
                                juce::dontSendNotification);
    signInCard.addAndMakeVisible (signInInstructions);

    /*  iPad ではブラウザ経路が構造的に不利になる。ChatGPT アプリ側で承認する
        経路があり、そのあいだアプリがバックグラウンドへ回ってループバックの
        待ち受けが止まる。デバイスコード経路はソケットを使わないので、
        アプリ間を行き来しても壊れない。iOS ではこちらを既定にする。 */
   #if JUCE_IOS
    constexpr auto primaryMethod   = SignInMethod::deviceCode;
    constexpr auto secondaryMethod = SignInMethod::browser;
   #else
    constexpr auto primaryMethod   = SignInMethod::browser;
    constexpr auto secondaryMethod = SignInMethod::deviceCode;
   #endif

    signInButton.onClick = [this] { startSignIn (primaryMethod); };
    signInCard.addAndMakeVisible (signInButton);

    // 代替経路。ChatGPT のセキュリティ設定でデバイスコード認証を有効にした
    // アカウントでしか使えないので、主ボタンの下に控えめに置く。
    deviceCodeButton.onClick = [this] { startSignIn (secondaryMethod); };
    signInCard.addAndMakeVisible (deviceCodeButton);

    copyCodeButton.onClick = [this]
    {
        const auto code = signInCode.getText().trim();

        if (code.isNotEmpty())
            juce::SystemClipboard::copyTextToClipboard (code);
    };
    signInCard.addChildComponent (copyCodeButton);

    openBrowserButton.onClick = [this]
    {
        if (verificationUrl.isEmpty())
            return;

        if (! juce::URL (verificationUrl).launchInDefaultBrowser())
        {
            signInInstructions.setText ("Could not open the browser. Open the verification URL manually.",
                                        juce::dontSendNotification);
            resized();
        }
    };
    signInCard.addChildComponent (openBrowserButton);

    cancelSignInButton.onClick = [this] { cancelSignIn(); };
    signInCard.addChildComponent (cancelSignInButton);

    addAndMakeVisible (signInCard);

    historyContent = std::make_unique<ChatHistoryView> ([this]
    {
        if (historyContent != nullptr)
            historyContent->setSize (juce::jmax (1, historyViewport.getWidth()),
                                     historyContent->getContentHeight());
        historyViewport.setViewPosition (historyViewport.getViewPosition());
    });
    historyViewport.setViewedComponent (historyContent.get(), false);
    historyViewport.setScrollBarsShown (true, false);
    addAndMakeVisible (historyViewport);

    approvalTitle.setJustificationType (juce::Justification::centredLeft);
    approvalTitle.setText ("Review change", juce::dontSendNotification);
    approvalCard.addAndMakeVisible (approvalTitle);

    approvalDiffContent = std::make_unique<DiffPreviewView>();
    approvalDiffViewport.setViewedComponent (approvalDiffContent.get(), false);
    approvalDiffViewport.setScrollBarsShown (true, false);
    approvalCard.addAndMakeVisible (approvalDiffViewport);

    approveButton.onClick = [this] { session->resolveApproval (true); };
    approvalCard.addAndMakeVisible (approveButton);

    rejectButton.onClick = [this] { session->resolveApproval (false); };
    approvalCard.addAndMakeVisible (rejectButton);

    autoApproveToggle.setToggleState (session->getAutoApprove(), juce::dontSendNotification);
    autoApproveToggle.onClick = [this]
    {
        session->setAutoApprove (autoApproveToggle.getToggleState());
    };
    approvalCard.addAndMakeVisible (autoApproveToggle);

    addChildComponent (approvalCard);

    input.setMultiLine (true, true);
    input.setReturnKeyStartsNewLine (false);
    input.setTextToShowWhenEmpty ("Describe what you want to change...", juce::Colours::grey);

    // 打った文字と、その結果が出る場所の見た目を揃える。
    input.setFont (getChatFont());
    input.setColour (juce::TextEditor::backgroundColourId, chatBackground);
    input.setColour (juce::TextEditor::textColourId, chatForeground);
    input.setColour (juce::TextEditor::outlineColourId, chatForeground.withAlpha (0.25f));
    input.setColour (juce::CaretComponent::caretColourId, chatForeground);

    /*  Enter で送信、Shift+Enter と Ctrl+J で改行、Escape で停止。
        改行の手段が無いと、複数行の指示が書けない。 */
    input.onReturnKey = [this] { sendCurrentInput(); };
    input.onEscapeKey = [this] { if (session->isBusy()) session->stop(); };
    input.addKeyListener (this);

    addFileButton = std::make_unique<RoundIconButton> ("Add files", RoundIconButton::Icon::plus);
    addFileButton->setTooltip ("Mention files in your message");
    addFileButton->onClick = [this] { chooseFilesToMention(); };
    addAndMakeVisible (*addFileButton);

    permissionButton = std::make_unique<FlatButton> ("Ask for approval");
    permissionButton->setLeadingGlyph (juce::String (juce::CharPointer_UTF8 ("\xe2\x9c\x8b")));
    permissionButton->onClick = [this] { showPermissionMenu(); };
    addAndMakeVisible (*permissionButton);

    modelButton = std::make_unique<FlatButton> ("model");
    modelButton->setShowsChevron (true);
    modelButton->setTooltip ("Choose the model, reasoning effort and speed");
    modelButton->onClick = [this] { showModelPicker(); };
    addAndMakeVisible (*modelButton);

    sendStopButton = std::make_unique<RoundIconButton> ("Send", RoundIconButton::Icon::send);
    sendStopButton->onClick = [this]
    {
        if (session->isBusy())
            session->stop();
        else
            sendCurrentInput();
    };
    addAndMakeVisible (*sendStopButton);

    updateModelButton();
    updatePermissionButton();
    input.onReturnKey = [this] { sendCurrentInput(); };
    addAndMakeVisible (input);



    session->addChangeListener (this);
    rebuildHistory();
    updateVisibility();
}

AiChatView::~AiChatView()
{
    if (signInStopToken != nullptr)
        signInStopToken->store (true, std::memory_order_release);

    lifetimeToken->store (false, std::memory_order_release);

    stopSignInWorker();

    session->removeChangeListener (this);
}

//==============================================================================
void AiChatView::startSignIn (SignInMethod method)
{
    stopSignInWorker();

    if (SignInWorker::hasRunningRetainedWorker())
        return;

    signInStopToken = std::make_shared<std::atomic<bool>> (false);
    const auto stopToken = signInStopToken;
    const auto viewLifetime = lifetimeToken;
    const juce::Component::SafePointer<AiChatView> view (this);

    const auto usingBrowser = method == SignInMethod::browser;

    signInButton.setEnabled (false);
    deviceCodeButton.setVisible (false);
    copyCodeButton.setVisible (false);
    openBrowserButton.setVisible (false);
    cancelSignInButton.setVisible (false);
    signInCode.setText ({}, juce::dontSendNotification);
    verificationUrl.clear();
    signInInstructions.setText (usingBrowser ? "Opening the ChatGPT sign-in page..."
                                             : "Requesting a device code...",
                                juce::dontSendNotification);
    resized();

    signInWorker = std::make_unique<SignInWorker> (auth, stopToken, viewLifetime, view, method);
    const auto launched = signInWorker->startThread();

    if (! launched)
    {
        stopToken->store (true, std::memory_order_release);
        stopSignInWorker();
        signInInstructions.setText ("Could not start the sign-in worker. Please try again.",
                                    juce::dontSendNotification);
        signInButton.setEnabled (true);
        resized();
    }
}

void AiChatView::stopSignInWorker()
{
    if (signInWorker == nullptr)
        return;

    auto worker = std::move (signInWorker);
    worker->requestStop();

    if (worker->waitForThreadToExit (0))
        return;

    SignInWorker::retain (std::move (worker));
}

void AiChatView::cancelSignIn()
{
    if (signInWorker != nullptr)
        signInWorker->requestStop();
    else if (signInStopToken != nullptr)
        signInStopToken->store (true, std::memory_order_release);

    signInInstructions.setText ("Cancelling sign-in...", juce::dontSendNotification);
    cancelSignInButton.setEnabled (false);
    openBrowserButton.setEnabled (false);
}

//==============================================================================
void AiChatView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    rebuildHistory();
    updateVisibility();
}

void AiChatView::rebuildHistory()
{
    juce::String text;

    for (const auto& entry : session->getEntries())
        text << prefixFor (entry.kind) << entry.text << "\n\n";

    juce::ignoreUnused (text);
    historyContent->setEntries (session->getEntries());
    historyContent->setSize (juce::jmax (1, historyViewport.getWidth()),
                             historyContent->getContentHeight());
}

void AiChatView::updateVisibility()
{
    const auto signedIn = auth->isSignedIn();
    const auto busy = session->isBusy();
    const auto* approval = session->getPendingApproval();

    signInCard.setVisible (! signedIn);
    historyViewport.setVisible (signedIn);
    input.setVisible (signedIn);
    input.setEnabled (signedIn && ! busy);
    /*  送信と停止は同じ丸ボタン。実行中は停止に変わる。押す場所が動かないので、
        止めたいときに探さずに済む。 */
    sendStopButton->setVisible (signedIn);
    sendStopButton->setIcon (busy ? RoundIconButton::Icon::stop : RoundIconButton::Icon::send);
    sendStopButton->setEnabled (signedIn);

    addFileButton->setVisible (signedIn);
    permissionButton->setVisible (signedIn);
    modelButton->setVisible (signedIn);
    approvalCard.setVisible (signedIn && approval != nullptr);

    if (approval != nullptr)
    {
        approvalTitle.setText ("Review " + approval->toolName + " change",
                               juce::dontSendNotification);
        approvalDiffContent->setText (approval->diffPreview);
    }

    autoApproveToggle.setToggleState (session->getAutoApprove(), juce::dontSendNotification);
    resized();
    repaint();
}

bool AiChatView::keyPressed (const juce::KeyPress& key, juce::Component* origin)
{
    if (origin != &input)
        return false;

    const auto isReturn = key.getKeyCode() == juce::KeyPress::returnKey;
    const auto wantsNewLine = (isReturn && key.getModifiers().isShiftDown())
                            || (key.getModifiers().isCtrlDown()
                                && key.getKeyCode() == 'J');

    if (! wantsNewLine)
        return false;

    input.insertTextAtCaret ("\n");
    return true;
}

void AiChatView::sendCurrentInput()
{
    if (! auth->isSignedIn() || session->isBusy())
        return;

    const auto text = input.getText();

    if (text.trim().isEmpty())
        return;

    if (handleSlashCommand (text.trim()))
    {
        input.clear();
        return;
    }

    const auto attachments = pendingAttachments;
    pendingAttachments.clear();

    input.clear();
    session->sendMessage (text, attachments);
}

/*  ローカルで処理するコマンド。モデルへは送らない。

    @returns  コマンドとして処理したら true。
*/
bool AiChatView::handleSlashCommand (const juce::String& text)
{
    if (! text.startsWith ("/"))
        return false;

    const auto command = text.upToFirstOccurrenceOf (" ", false, false).toLowerCase();
    const auto argument = text.fromFirstOccurrenceOf (" ", false, false).trim();

    if (command == "/model")
    {
        showModelPicker();
        return true;
    }

    if (command == "/help")
    {
        session->addLocalNotice ("Commands:\n"
                                 "  /model  choose the model and reasoning effort\n"
                                 "  /help   show this list\n"
                                 "\nCurrent model: " + AiModels::describeSelection());
        return true;
    }

    // 知らない /... はコマンドではなく本文かもしれないので、そのまま送らせる。
    return false;
}

/*  Codex の /model と同じ二段構成。まずモデルを選び、続けて推論の強さを選ぶ。
    自由入力にしないのは、綴りの誤りが 400 になって初めて分かるため。
*/
/*  Codex デスクトップ版と同じ構成のメニュー。

    モデル / 推論レベル / 速度 の 3 項目を並べ、それぞれサブメニューで選ばせる。
    末尾に「デフォルトに戻す」を置く。TUI 版の /model は二段階だが、GUI では
    どの項目も対等に触れる方が扱いやすいので、デスクトップ版に合わせる。
*/
void AiChatView::showModelPicker()
{
    const auto currentModel  = AiModels::getSelectedModel();
    const auto currentEffort = AiModels::getSelectedEffort();
    const auto currentTier   = AiModels::getSelectedSpeedTier();

    const auto models  = AiModels::getKnownModels();
    const auto efforts = AiModels::getEffortsFor (currentModel);
    const auto tiers   = AiModels::getSpeedTiersFor (currentModel);

    juce::PopupMenu modelMenu;

    for (int i = 0; i < models.size(); ++i)
    {
        const auto& model = models.getReference (i);

        juce::PopupMenu::Item item;
        item.itemID = modelBaseId + i;
        item.text = model.displayName;
        item.isTicked = currentModel == model.id;
        modelMenu.addItem (item);
    }

    juce::PopupMenu effortMenu;

    for (int i = 0; i < efforts.size(); ++i)
    {
        const auto& effort = efforts.getReference (i);

        juce::PopupMenu::Item item;
        item.itemID = effortBaseId + i;
        item.text = effort.label;
        item.isTicked = currentEffort == effort.id;
        effortMenu.addItem (item);
    }

    juce::PopupMenu speedMenu;

    for (int i = 0; i < tiers.size(); ++i)
    {
        const auto& tier = tiers.getReference (i);

        juce::PopupMenu::Item item;
        item.itemID = speedBaseId + i;
        item.text = juce::String (tier.label) + "   " + tier.description;
        item.isTicked = currentTier == tier.id;
        speedMenu.addItem (item);
    }

    juce::PopupMenu menu;
    menu.addSubMenu ("Model           " + AiModels::displayNameFor (currentModel), modelMenu);
    menu.addSubMenu ("Reasoning       " + AiModels::labelForEffort (currentEffort), effortMenu);
    menu.addSubMenu ("Speed           " + AiModels::labelForSpeedTier (currentTier), speedMenu);
    menu.addSeparator();
    menu.addItem (resetId, "Reset to default");

    juce::Component::SafePointer<AiChatView> safeThis (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (static_cast<juce::Component*> (modelButton.get())),
                        [safeThis, models, efforts, tiers] (int chosen)
    {
        if (safeThis == nullptr || chosen <= 0)
            return;

        if (chosen == resetId)
        {
            AiModels::setSelection (AiModels::getDefaultModel(), {});
            AiModels::setSelectedSpeedTier ({});
        }
        else if (chosen >= modelBaseId && chosen < modelBaseId + models.size())
        {
            // モデルを変えたら effort はそのモデルの既定へ戻す。
            AiModels::setSelection (models.getReference (chosen - modelBaseId).id, {});
        }
        else if (chosen >= effortBaseId && chosen < effortBaseId + efforts.size())
        {
            AiModels::setSelection (AiModels::getSelectedModel(),
                                    efforts.getReference (chosen - effortBaseId).id);
        }
        else if (chosen >= speedBaseId && chosen < speedBaseId + tiers.size())
        {
            AiModels::setSelectedSpeedTier (tiers.getReference (chosen - speedBaseId).id);
        }
        else
        {
            return;
        }

        safeThis->updateModelButton();
    });
}

juce::File AiChatView::projectRootForChooser() const
{
    return session->getProjectRoot();
}

void AiChatView::updateModelButton()
{
    // 「GPT-5.6-Luna」を主、「Medium」を淡い補足として並べる。
    modelButton->setButtonText (AiModels::displayNameFor (AiModels::getSelectedModel()));

    auto detail = AiModels::labelForEffort (AiModels::getSelectedEffort());
    const auto tier = AiModels::getSelectedSpeedTier();

    if (tier.isNotEmpty())
        detail << " / " << AiModels::labelForSpeedTier (tier);

    modelButton->setDetailText (detail);
    resized();
}

void AiChatView::updatePermissionButton()
{
    /*  書き込みの扱いを示す。既定は毎回確認で、自動承認にすると
        差分カードを出さずに適用する。 */
    switch (session->getApprovalMode())
    {
        case AiSession::ApprovalMode::ask:      permissionButton->setButtonText ("Ask for approval"); break;
        case AiSession::ApprovalMode::onUnsafe: permissionButton->setButtonText ("Approve on my behalf"); break;
        case AiSession::ApprovalMode::full:     permissionButton->setButtonText ("Full access"); break;
    }
    resized();
}

void AiChatView::showPermissionMenu()
{
    /*  ChatGPT の「ChatGPT のアクションの承認方法」と同じ 3 段階。 */
    const auto mode = session->getApprovalMode();

    juce::PopupMenu menu;
    menu.addSectionHeader ("How to approve actions");

    const auto addChoice = [&menu, mode] (int id, AiSession::ApprovalMode value,
                                          const juce::String& title, const juce::String& detail)
    {
        juce::PopupMenu::Item item;
        item.itemID = id;
        item.text = title + "\n" + detail;
        item.isTicked = mode == value;
        menu.addItem (item);
    };

    addChoice (1, AiSession::ApprovalMode::ask,
               "Ask for approval",
               "Always confirm before any file is written");
    addChoice (2, AiSession::ApprovalMode::onUnsafe,
               "Approve on my behalf",
               "Only confirm changes that could lose work, such as creating or replacing a file");
    addChoice (3, AiSession::ApprovalMode::full,
               "Full access",
               "Never confirm, and allow writes outside the project");

    juce::Component::SafePointer<AiChatView> safeThis (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (static_cast<juce::Component*> (permissionButton.get())),
                        [safeThis] (int chosen)
    {
        if (safeThis == nullptr || chosen <= 0)
            return;

        const auto mode = chosen == 1 ? AiSession::ApprovalMode::ask
                        : chosen == 2 ? AiSession::ApprovalMode::onUnsafe
                                      : AiSession::ApprovalMode::full;

        safeThis->session->setApprovalMode (mode);
        safeThis->updatePermissionButton();
    });
}

void AiChatView::chooseFilesToMention()
{
    /*  選んだファイルを入力欄へ差し込むだけ。読むかどうかはモデルが決める。
        ここでファイルを読んで送ると、要らない内容まで毎回送ることになる。 */
    auto chooser = std::make_shared<juce::FileChooser> ("Mention files",
                                                        projectRootForChooser(),
                                                        "*");

    const auto flags = juce::FileBrowserComponent::openMode
                     | juce::FileBrowserComponent::canSelectFiles
                     | juce::FileBrowserComponent::canSelectMultipleItems;

    juce::Component::SafePointer<AiChatView> safeThis (this);

    chooser->launchAsync (flags, [safeThis, chooser] (const juce::FileChooser& fc)
    {
        if (safeThis == nullptr)
            return;

        juce::StringArray names;

        for (const auto& file : fc.getResults())
        {
            names.add (file.getRelativePathFrom (safeThis->projectRootForChooser()));
            safeThis->pendingAttachments.addIfNotAlreadyThere (file);
        }

        if (names.isEmpty())
            return;

        auto text = safeThis->input.getText();

        if (text.isNotEmpty() && ! text.endsWithChar (' '))
            text << " ";

        safeThis->input.setText (text + names.joinIntoString (" ") + " ");
        safeThis->input.grabKeyboardFocus();
        safeThis->input.moveCaretToEnd();
    });
}

//==============================================================================
void AiChatView::paint (juce::Graphics& g)
{
    // ビューポートの余白まで含めて、テキストが出る領域はターミナルと同じ地色にする。
    g.fillAll (chatBackground);

    if (! separatorArea.isEmpty())
    {
        g.setColour (findColour (juce::ResizableWindow::backgroundColourId));
        g.fillRect (separatorArea);
    }

    const auto drawCard = [&g, this] (const juce::Rectangle<int>& bounds)
    {
        const auto card = bounds.toFloat().reduced (1.0f);
        g.setColour (findColour (juce::ResizableWindow::backgroundColourId).contrasting (0.08f));
        g.fillRoundedRectangle (card, 6.0f);
        g.setColour (findColour (juce::Label::textColourId).withAlpha (0.12f));
        g.drawRoundedRectangle (card, 6.0f, 1.0f);
    };

    if (signInCard.isVisible())
        drawCard (signInCard.getBounds());

    if (approvalCard.isVisible())
        drawCard (approvalCard.getBounds());
}

void AiChatView::resized()
{
    auto bounds = getLocalBounds().reduced (padding);

    if (signInCard.isVisible())
    {
        signInCard.setBounds (bounds);

        auto card = signInCard.getLocalBounds().reduced (padding);
        const auto contentWidth = juce::jmin (480, card.getWidth());
        const auto contentHeight = juce::jmin (360, card.getHeight());
        card = card.withSizeKeepingCentre (contentWidth, contentHeight);

        signInTitle.setBounds (card.removeFromTop (rowHeight));
        signInCode.setBounds (card.removeFromTop (rowHeight + 18));
        copyCodeButton.setBounds (card.removeFromTop (rowHeight).reduced (0, 2));
        signInInstructions.setBounds (card.removeFromTop (rowHeight + 34));
        signInButton.setBounds (card.removeFromTop (rowHeight).reduced (0, 2));

        if (deviceCodeButton.isVisible())
            deviceCodeButton.setBounds (card.removeFromTop (rowHeight).reduced (0, 2));

        if (openBrowserButton.isVisible())
            openBrowserButton.setBounds (card.removeFromTop (rowHeight).reduced (0, 2));

        if (cancelSignInButton.isVisible())
            cancelSignInButton.setBounds (card.removeFromTop (rowHeight).reduced (0, 2));

        return;
    }

    /*  入力欄と、その下の操作列。ChatGPT と同じ並びにする。
        左に「ファイル追加」と「承認の扱い」、右にモデル設定と送信/停止。 */
    {
        auto controlRow = bounds.removeFromBottom (controlRowHeight);

        auto rightSide = controlRow.removeFromRight (roundButtonSize + 4);
        sendStopButton->setBounds (rightSide.withSizeKeepingCentre (roundButtonSize, roundButtonSize));

        controlRow.removeFromRight (6);
        const auto modelWidth = juce::jmin (modelButton->getPreferredWidth(), controlRow.getWidth() / 2);
        modelButton->setBounds (controlRow.removeFromRight (modelWidth).reduced (0, 3));

        addFileButton->setBounds (controlRow.removeFromLeft (roundButtonSize + 4)
                                            .withSizeKeepingCentre (roundButtonSize, roundButtonSize));

        const auto permissionWidth = juce::jmin (permissionButton->getPreferredWidth(),
                                                 controlRow.getWidth());
        permissionButton->setBounds (controlRow.removeFromLeft (permissionWidth).reduced (0, 3));
    }

    input.setBounds (bounds.removeFromBottom (inputHeight - controlRowHeight + rowHeight).reduced (0, 2));

    /*  入力欄と履歴が地続きだと、どこまでが AI の出力か分かりにくい。
        Projucer の地色で帯を挟んで切り分ける。 */
    separatorArea = bounds.removeFromBottom (padding);

    bounds.removeFromBottom (padding / 2);

    if (approvalCard.isVisible())
    {
        auto approvalHeight = juce::jmin (260, juce::jmax (140, bounds.getHeight() / 2));
        approvalHeight = juce::jmin (approvalHeight, bounds.getHeight());
        auto cardArea = bounds.removeFromBottom (approvalHeight);
        approvalCard.setBounds (cardArea);

        auto card = approvalCard.getLocalBounds().reduced (4);
        approvalTitle.setBounds (card.removeFromTop (rowHeight));

        auto actionArea = card.removeFromBottom (rowHeight);
        approveButton.setBounds (actionArea.removeFromLeft (90).reduced (2));
        rejectButton.setBounds (actionArea.removeFromLeft (90).reduced (2));
        autoApproveToggle.setBounds (actionArea.reduced (2));
        approvalDiffViewport.setBounds (card.reduced (0, 4));
        bounds.removeFromBottom (padding / 2);
    }

    historyViewport.setBounds (bounds);

    if (historyContent != nullptr)
        historyContent->setSize (juce::jmax (1, historyViewport.getWidth()),
                                 historyContent->getContentHeight());
}
