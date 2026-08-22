#include "../Application/jucer_Headers.h"

#include "jucer_AiChatView.h"

#include "jucer_AiModels.h"
#include "jucer_AuthBrowser.h"

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
        runOnAppMainThread ([lifetime, view, fn = std::forward<Fn> (fn)]() mutable
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
        if (highlighted || down || getToggleState())
        {
            g.setColour (chatForeground.withAlpha (down ? 0.16f : getToggleState() ? 0.14f : 0.10f));
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
        return getContentHeightForWidth (contentWidth());
    }

    int getContentHeightForWidth (int width) const
    {
        auto height = 8;

        for (int i = 0; i < entries.size(); ++i)
            height += measureEntry (entries.getReference (i), expanded[i], width);

        return height + 8;
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (chatBackground);

        auto y = 8;
        const auto width = contentWidth();

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
                    const auto bodyWidth = juce::jmax (1, width - 16);
                    const auto bodyHeight = textHeight (entry.text, bodyWidth);
                    layoutFor (entry.text, (float) bodyWidth, chatForeground)
                        .draw (g, juce::Rectangle<float> (16.0f, (float) y + 2.0f,
                                                          (float) bodyWidth, (float) bodyHeight));
                    y += bodyHeight + 4;
                }
            }
            else if (entry.kind == AiSession::Entry::Kind::user)
            {
                /*  自分の発言は右寄せのバブル。幅は本文に合わせ、短い行が
                    画面中央に浮かないようにする。 */
                const auto bubble = userBubbleBounds (entry.text, width, y);

                g.setColour (userBubble);
                g.fillRoundedRectangle (bubble.toFloat(), 10.0f);

                layoutFor (entry.text, (float) juce::jmax (1, bubble.getWidth() - bubblePadX * 2), userText)
                    .draw (g, bubble.reduced (bubblePadX, bubblePadY).toFloat());
                y += bubble.getHeight();
            }
            else
            {
                const auto colour = entry.kind == AiSession::Entry::Kind::error
                                         ? juce::Colours::indianred
                                         : chatForeground;
                const auto height = textHeight (entry.text, width);
                layoutFor (entry.text, (float) width, colour)
                    .draw (g, juce::Rectangle<float> (8.0f, (float) y, (float) width, (float) height));
                y += height;
            }

            y += 6;
        }
    }

    void mouseUp (const juce::MouseEvent& event) override
    {
        auto y = 8;
        const auto width = contentWidth();

        for (int i = 0; i < entries.size(); ++i)
        {
            const auto& entry = entries.getReference (i);

            if (entry.kind == AiSession::Entry::Kind::tool)
            {
                if (juce::Rectangle<int> (8, y, width, toolHeaderHeight).contains (event.getPosition()))
                {
                    expanded.set (i, ! expanded[i]);
                    if (heightChanged != nullptr)
                        heightChanged();
                    repaint();
                    return;
                }
            }

            y += measureEntry (entry, expanded[i], width);
        }
    }

private:
    static constexpr int toolHeaderHeight = 30;
    static constexpr int bubblePadX = 12;
    static constexpr int bubblePadY = 6;

    int contentWidth() const
    {
        return juce::jmax (1, getWidth() - 16);
    }

    static int maxUserBubbleWidth (int availableWidth)
    {
        return juce::jmin (availableWidth - 24, juce::jmax (48, availableWidth * 3 / 4));
    }

    static juce::Rectangle<int> userBubbleBounds (const juce::String& text, int availableWidth, int y)
    {
        const auto maxWidth = maxUserBubbleWidth (availableWidth);
        const auto maxInner = juce::jmax (1, maxWidth - bubblePadX * 2);
        const auto layout = layoutFor (text, (float) maxInner, userText);
        const auto innerW = juce::jlimit (1, maxInner,
                                          (int) std::ceil ((double) layout.getWidth()));
        const auto innerH = juce::jmax (16, (int) std::ceil ((double) layout.getHeight()));
        const auto bubbleW = juce::jmax (innerW + bubblePadX * 2, 36);
        const auto bubbleH = juce::jmax (innerH + bubblePadY * 2, 28);

        return { 8 + availableWidth - bubbleW, y, bubbleW, bubbleH };
    }

    static juce::TextLayout layoutFor (const juce::String& text, float width, juce::Colour colour)
    {
        juce::AttributedString as;
        as.setJustification (juce::Justification::topLeft);
        as.append (text.isNotEmpty() ? text : " ", getChatFont(), colour);
        as.setLineSpacing (2.0f);

        juce::TextLayout layout;
        layout.createLayout (as, juce::jmax (1.0f, width));
        return layout;
    }

    static int textHeight (const juce::String& text, int width)
    {
        const auto layout = layoutFor (text, (float) juce::jmax (1, width), chatForeground);
        return juce::jmax (24, (int) std::ceil ((double) layout.getHeight()) + 8);
    }

    int measureEntry (const AiSession::Entry& entry, bool isExpanded, int width) const
    {
        if (entry.kind == AiSession::Entry::Kind::tool)
        {
            auto height = toolHeaderHeight;

            if (isExpanded)
                height += textHeight (entry.text, juce::jmax (1, width - 16)) + 4;

            return height + 6;
        }

        if (entry.kind == AiSession::Entry::Kind::user)
            return userBubbleBounds (entry.text, width, 0).getHeight() + 6;

        return textHeight (entry.text, width) + 6;
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
        sourceLines.clear();
        sourceLines.addLines (textToUse.replace ("\r", {}));
        setSize (juce::jmax (1, getWidth()), getContentHeight());
        repaint();
    }

    int getContentHeight() const noexcept
    {
        return juce::jmax (lineHeight + 8, wrappedLineCount() * lineHeight + 8);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (chatBackground);
        const auto font = previewFont();
        g.setFont (font);

        const auto display = wrappedLines();
        const auto textWidth = juce::jmax (1, getWidth() - 16);

        for (int i = 0; i < display.size(); ++i)
        {
            const auto& line = display[i];
            auto colour = chatForeground;

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
            g.drawText (line, 8, i * lineHeight, textWidth, lineHeight,
                        juce::Justification::centredLeft, false);
        }
    }

private:
    static constexpr int lineHeight = 20;

    static juce::Font previewFont()
    {
        return juce::Font { juce::FontOptions { juce::Font::getDefaultMonospacedFontName(), 13.0f, 0 } };
    }

    int wrappedLineCount() const noexcept
    {
        return wrappedLines().size();
    }

    juce::StringArray wrappedLines() const
    {
        juce::StringArray wrapped;
        const auto font = previewFont();
        const auto maxWidth = (float) juce::jmax (8, getWidth() - 16);

        for (const auto& line : sourceLines)
        {
            if (line.isEmpty()
                || juce::GlyphArrangement::getStringWidth (font, line) <= maxWidth)
            {
                wrapped.add (line);
                continue;
            }

            auto remaining = line;

            while (remaining.isNotEmpty())
            {
                int low = 1, high = remaining.length(), fit = 1;

                while (low <= high)
                {
                    const auto mid = (low + high) / 2;

                    if (juce::GlyphArrangement::getStringWidth (font, remaining.substring (0, mid)) <= maxWidth)
                    {
                        fit = mid;
                        low = mid + 1;
                    }
                    else
                    {
                        high = mid - 1;
                    }
                }

                wrapped.add (remaining.substring (0, fit));
                remaining = remaining.substring (fit);
            }
        }

        return wrapped.isEmpty() ? juce::StringArray { juce::String() } : wrapped;
    }

    juce::StringArray sourceLines;
};

class AiChatView::SignInWorker final : public juce::Thread
{
public:
    SignInWorker (std::shared_ptr<CodexAuth> chatgptAuthToUse,
                  std::shared_ptr<GrokAuth> grokAuthToUse,
                  SignInProvider providerToUse,
                  const std::shared_ptr<std::atomic<bool>>& stopTokenToUse,
                  const std::shared_ptr<std::atomic<bool>>& lifetimeToUse,
                  juce::Component::SafePointer<AiChatView> viewToUse,
                  SignInMethod methodToUse)
        : juce::Thread ("AI Sign-In"),
          chatgptAuth (std::move (chatgptAuthToUse)),
          grokAuth (std::move (grokAuthToUse)),
          provider (providerToUse),
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

        if (provider == SignInProvider::grok)
            grokAuth->cancelActiveRequest();
        else
            chatgptAuth->cancelActiveRequest();
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
        callOnLiveView (lifetimeToken, view, [isGrok = provider == SignInProvider::grok] (AiChatView& liveView)
        {
            if (isGrok && liveView.grokAuth->isSignedIn())
            {
                liveView.updateVisibility();
                return;
            }

            if (isGrok)
            {
                liveView.signInInstructions.setText (
                    "Finish signing in in the page above. If it shows a code, paste it below. "
                    "Pull the page down if it covers this field.",
                    juce::dontSendNotification);
                liveView.signInButton.setVisible (false);
                liveView.grokSignInButton.setVisible (false);
                liveView.grokPasteEditor.clear();
                liveView.grokPasteEditor.setVisible (true);
                liveView.grokPasteButton.setVisible (true);
                liveView.grokPasteButton.setEnabled (true);
            }
            else
            {
                liveView.signInInstructions.setText ("Waiting for you to finish signing in...",
                                                     juce::dontSendNotification);
                liveView.grokPasteEditor.setVisible (false);
                liveView.grokPasteButton.setVisible (false);
            }

            liveView.cancelSignInButton.setVisible (true);
            liveView.cancelSignInButton.setEnabled (true);
            liveView.resized();
        });

        juce::String error;
        const auto signedIn = provider == SignInProvider::grok
                                ? grokAuth->signInWithBrowser (*stopToken, error)
                                : chatgptAuth->signInWithBrowser (*stopToken, error);

        if (! lifetimeToken->load (std::memory_order_acquire))
            return;

        if (signedIn)
        {
            callOnLiveView (lifetimeToken, view, [isGrok = provider == SignInProvider::grok] (AiChatView& liveView)
            {
                if (isGrok && AiModels::getSelectedProvider() != AiModels::Provider::grok)
                    AiModels::setSelection (AiModels::getDefaultModelFor (AiModels::Provider::grok), {});

                liveView.signInButton.setEnabled (true);
                liveView.grokSignInButton.setEnabled (true);
                liveView.grokPasteEditor.setVisible (false);
                liveView.grokPasteButton.setVisible (false);
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

        callOnLiveView (lifetimeToken, view, [displayError,
                                              isGrok = provider == SignInProvider::grok] (AiChatView& liveView)
        {
            liveView.signInInstructions.setText (displayError, juce::dontSendNotification);

            /*  ここで updateVisibility() は呼べない。あれは「サインイン中か」を
                ワーカースレッドが生きているかで見るが、このラムダはそのワーカー
                自身から投げられるので、まだ生きている扱いになりボタンを全部
                隠してしまう。可視状態はここで直接戻す。 */
            liveView.signInButton.setVisible (! isGrok);
            liveView.signInButton.setEnabled (true);
            liveView.grokSignInButton.setVisible (isGrok);
            liveView.grokSignInButton.setEnabled (true);
            liveView.deviceCodeButton.setVisible (liveView.providerShowsDeviceCode());
            liveView.grokPasteEditor.setVisible (false);
            liveView.grokPasteButton.setVisible (false);
            liveView.cancelSignInButton.setVisible (false);
            liveView.resized();
        });
    }

    void runDeviceCodeSignIn()
    {
        juce::String error;
        const auto code = chatgptAuth->requestDeviceCode (error, stopToken.get());

        if (! lifetimeToken->load (std::memory_order_acquire))
            return;

        if (stopToken->load (std::memory_order_acquire))
        {
            callOnLiveView (lifetimeToken, view, [] (AiChatView& liveView)
            {
                liveView.signInInstructions.setText ("Sign-in was cancelled.",
                                                     juce::dontSendNotification);
                liveView.signInButton.setEnabled (true);
                liveView.grokSignInButton.setEnabled (true);
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
                liveView.grokSignInButton.setEnabled (true);
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

        const auto signedIn = chatgptAuth->pollForTokens (deviceCode, *stopToken, error);

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
            liveView.grokSignInButton.setEnabled (true);
            liveView.copyCodeButton.setVisible (false);
            liveView.openBrowserButton.setVisible (false);
            liveView.cancelSignInButton.setVisible (false);
            liveView.cancelSignInButton.setEnabled (true);
            liveView.openBrowserButton.setEnabled (true);
            liveView.resized();
        });
    }

private:
    std::shared_ptr<CodexAuth> chatgptAuth;
    std::shared_ptr<GrokAuth> grokAuth;
    SignInProvider provider = SignInProvider::chatgpt;
    std::shared_ptr<std::atomic<bool>> stopToken;
    std::shared_ptr<std::atomic<bool>> lifetimeToken;
    juce::Component::SafePointer<AiChatView> view;
    SignInMethod method = SignInMethod::browser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SignInWorker)
};

//==============================================================================
AiChatView::AiChatView (std::shared_ptr<AiSession> sessionToUse,
                        std::shared_ptr<CodexAuth> chatgptAuthToUse,
                        std::shared_ptr<GrokAuth> grokAuthToUse)
    : session (std::move (sessionToUse)),
      chatgptAuth (std::move (chatgptAuthToUse)),
      grokAuth (std::move (grokAuthToUse)),
      lifetimeToken (std::make_shared<std::atomic<bool>> (true))
{
    setTitle ("AI Chat");
    setOpaque (false);

    signInTitle.setText ("Sign in", juce::dontSendNotification);
    signInTitle.setJustificationType (juce::Justification::centred);
    signInTitle.setFont (juce::FontOptions { 18.0f, juce::Font::bold });
    signInCard.addAndMakeVisible (signInTitle);

    signInCode.setJustificationType (juce::Justification::centred);
    signInCode.setFont (juce::FontOptions { 28.0f, juce::Font::bold });
    signInCard.addAndMakeVisible (signInCode);

    signInInstructions.setJustificationType (juce::Justification::centred);
    signInInstructions.setMinimumHorizontalScale (0.8f);
    signInInstructions.setText ("Sign in with ChatGPT or Grok to use the assistant.",
                                juce::dontSendNotification);
    signInCard.addAndMakeVisible (signInInstructions);

    /*  iPad ではブラウザ経路が構造的に不利になる。ChatGPT アプリ側で承認する
        経路があり、そのあいだアプリがバックグラウンドへ回ってループバックの
        待ち受けが止まる。デバイスコード経路はソケットを使わないので、
        アプリ間を行き来しても壊れない。iOS ではこちらを既定にする。 */
    signInButton.onClick = [this]
    {
        startSignIn (SignInProvider::chatgpt, defaultSignInMethodFor (SignInProvider::chatgpt));
    };
    signInCard.addAndMakeVisible (signInButton);

    grokSignInButton.onClick = [this] { startSignIn (SignInProvider::grok, SignInMethod::browser); };
    signInCard.addAndMakeVisible (grokSignInButton);

    // 代替経路。ChatGPT のセキュリティ設定でデバイスコード認証を有効にした
    // アカウントでしか使えないので、主ボタンの下に控えめに置く。
    deviceCodeButton.onClick = [this]
    {
        const auto usual = defaultSignInMethodFor (SignInProvider::chatgpt);
        startSignIn (SignInProvider::chatgpt,
                     usual == SignInMethod::browser ? SignInMethod::deviceCode
                                                    : SignInMethod::browser);
    };
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

    grokPasteEditor.setMultiLine (false);
    grokPasteEditor.setReturnKeyStartsNewLine (false);
    grokPasteEditor.setTextToShowWhenEmpty ("Paste the code from the browser",
                                            chatForeground.withAlpha (0.4f));
    grokPasteEditor.addKeyListener (this);
    grokPasteEditor.onReturnKey = [this] { submitGrokPaste(); };
    grokPasteEditor.onTextChange = [this]
    {
        const auto pasted = grokPasteEditor.getText().trim();

        /*  iPad はキーボードで Continue が隠れる。長いコードが一気に入ったら
            そのまま進める。 */
        if (pasted.length() >= 24 && ! pasted.containsChar (' '))
            submitGrokPaste();
    };
    signInCard.addChildComponent (grokPasteEditor);

    grokPasteButton.onClick = [this] { submitGrokPaste(); };
    signInCard.addChildComponent (grokPasteButton);

    addAndMakeVisible (signInCard);

    historyContent = std::make_unique<ChatHistoryView> ([this]
    {
        updateHistoryLayout (false);
    });
    historyViewport.setViewedComponent (historyContent.get(), false);
    historyViewport.setScrollBarsShown (false, false, true, false);
    addAndMakeVisible (historyViewport);

    approvalTitle.setJustificationType (juce::Justification::centredLeft);
    approvalTitle.setText ("Review change", juce::dontSendNotification);
    approvalCard.addAndMakeVisible (approvalTitle);

    approvalDiffContent = std::make_unique<DiffPreviewView>();
    approvalDiffViewport.setViewedComponent (approvalDiffContent.get(), false);
    approvalDiffViewport.setScrollBarsShown (true, false);
    approvalCard.addAndMakeVisible (approvalDiffViewport);

    approveButton.onClick = [this]
    {
        if (session->getPendingApproval() != nullptr
            && session->getPendingApproval()->toolName == "exec_command")
            session->setExecDestination (AiSession::ExecDestination::subprocess);

        session->resolveApproval (true);
    };
    approvalCard.addAndMakeVisible (approveButton);

    runInTerminalButton.onClick = [this]
    {
        session->setExecDestination (AiSession::ExecDestination::visibleTerminal);
        session->resolveApproval (true);
    };
    approvalCard.addChildComponent (runInTerminalButton);

    rejectButton.onClick = [this] { session->resolveApproval (false); };
    approvalCard.addAndMakeVisible (rejectButton);

    autoApproveToggle.setToggleState (session->getAutoApprove(), juce::dontSendNotification);
    autoApproveToggle.onClick = [this]
    {
        session->setAutoApprove (autoApproveToggle.getToggleState());
    };
    approvalCard.addAndMakeVisible (autoApproveToggle);

    for (auto* button : { static_cast<juce::Component*> (&approveButton),
                          static_cast<juce::Component*> (&runInTerminalButton),
                          static_cast<juce::Component*> (&rejectButton),
                          static_cast<juce::Component*> (&autoApproveToggle) })
    {
        button->setWantsKeyboardFocus (false);
        button->setMouseClickGrabsKeyboardFocus (false);
    }

    addChildComponent (approvalCard);

    input.setMultiLine (true, true);
    input.setReturnKeyStartsNewLine (false);
    input.setTextToShowWhenEmpty ("Describe what you want to change...", juce::Colours::grey);

    // 打った文字と、その結果が出る場所の見た目を揃える。
    input.setFont (getChatFont());
    input.setColour (juce::TextEditor::backgroundColourId, chatBackground);
    input.setColour (juce::TextEditor::textColourId, chatForeground);
    input.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    input.setColour (juce::TextEditor::focusedOutlineColourId, juce::Colours::transparentBlack);
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

    execTargetButton = std::make_unique<FlatButton> ("Terminal");
    execTargetButton->setClickingTogglesState (true);
    execTargetButton->setTooltip ("Run commands in the bottom terminal");
    execTargetButton->onClick = [this]
    {
        session->setExecDestination (execTargetButton->getToggleState()
            ? AiSession::ExecDestination::visibleTerminal
            : AiSession::ExecDestination::subprocess);
        updateExecTargetButton();
    };
    addAndMakeVisible (*execTargetButton);

    modelButton = std::make_unique<FlatButton> ("model");
    modelButton->setShowsChevron (true);
    modelButton->setTooltip ("Choose the model, or sign in again");
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
    updateExecTargetButton();
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
bool AiChatView::isSelectedProviderSignedIn() const
{
    return AiModels::getSelectedProvider() == AiModels::Provider::grok
             ? grokAuth->isSignedIn()
             : chatgptAuth->isSignedIn();
}

bool AiChatView::providerShowsDeviceCode() const
{
    return AiModels::getSelectedProvider() != AiModels::Provider::grok;
}

AiChatView::SignInMethod AiChatView::defaultSignInMethodFor (SignInProvider provider) const
{
    if (provider == SignInProvider::grok)
        return SignInMethod::browser;

   #if JUCE_IOS
    return SignInMethod::deviceCode;
   #else
    return SignInMethod::browser;
   #endif
}

void AiChatView::restartSignIn()
{
    session->stop();

    const auto grok = AiModels::getSelectedProvider() == AiModels::Provider::grok;
    const auto provider = grok ? SignInProvider::grok : SignInProvider::chatgpt;

    if (grok)
        grokAuth->signOut();
    else
        chatgptAuth->signOut();

    updateVisibility();
    startSignIn (provider, defaultSignInMethodFor (provider));
}

void AiChatView::startSignIn (SignInProvider provider, SignInMethod method)
{
    stopSignInWorker();

    if (SignInWorker::hasRunningRetainedWorker())
        return;

    if (provider == SignInProvider::grok)
        method = SignInMethod::browser;

    signInStopToken = std::make_shared<std::atomic<bool>> (false);
    const auto stopToken = signInStopToken;
    const auto viewLifetime = lifetimeToken;
    const juce::Component::SafePointer<AiChatView> view (this);

    const auto usingBrowser = method == SignInMethod::browser;

    signInButton.setEnabled (false);
    grokSignInButton.setEnabled (false);
    deviceCodeButton.setVisible (false);
    copyCodeButton.setVisible (false);
    openBrowserButton.setVisible (false);
    cancelSignInButton.setVisible (false);
    signInCode.setText ({}, juce::dontSendNotification);
    grokPasteEditor.setVisible (false);
    grokPasteButton.setVisible (false);
    grokPasteEditor.clear();
    verificationUrl.clear();
    signInInstructions.setText (usingBrowser
                                    ? (provider == SignInProvider::grok
                                           ? "Opening the Grok sign-in page..."
                                           : "Opening the ChatGPT sign-in page...")
                                    : "Requesting a device code...",
                                juce::dontSendNotification);
    resized();

    signInWorker = std::make_unique<SignInWorker> (chatgptAuth, grokAuth, provider,
                                                   stopToken, viewLifetime, view, method);
    const auto launched = signInWorker->startThread();

    if (! launched)
    {
        stopToken->store (true, std::memory_order_release);
        stopSignInWorker();
        signInInstructions.setText ("Could not start the sign-in worker. Please try again.",
                                    juce::dontSendNotification);
        signInButton.setEnabled (true);
        grokSignInButton.setEnabled (true);
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
    historyContent->setEntries (session->getEntries());
    updateHistoryLayout (session->isBusy());
}

void AiChatView::updateHistoryLayout (bool forceFollow)
{
    if (historyContent == nullptr)
        return;

    const auto shouldFollow = forceFollow || isHistoryNearBottom();
    const auto width = juce::jmax (1, historyViewport.getMaximumVisibleWidth());
    historyContent->setSize (width, historyContent->getContentHeightForWidth (juce::jmax (1, width - 16)));

    if (shouldFollow)
        scrollHistoryToBottom();
}

void AiChatView::scrollHistoryToBottom()
{
    if (historyContent == nullptr)
        return;

    historyViewport.setViewPosition (0, juce::jmax (0, historyContent->getHeight()
                                                         - historyViewport.getMaximumVisibleHeight()));
}

bool AiChatView::isHistoryNearBottom() const
{
    if (historyContent == nullptr)
        return true;

    const auto viewH = historyViewport.getMaximumVisibleHeight();
    const auto contentH = historyContent->getHeight();

    if (viewH <= 0 || contentH <= viewH + 2)
        return true;

    return historyViewport.getViewPositionY() + viewH >= contentH - 48;
}

void AiChatView::updateVisibility()
{
    const auto signedIn = isSelectedProviderSignedIn();
    const auto busy = session->isBusy();
    const auto* approval = session->getPendingApproval();
    const auto selectedGrok = AiModels::getSelectedProvider() == AiModels::Provider::grok;
    const auto signingIn = signInWorker != nullptr && signInWorker->isThreadRunning();

    if (selectedGrok)
    {
        signInTitle.setText ("Sign in to Grok", juce::dontSendNotification);
        if (! signingIn)
            signInInstructions.setText ("Sign in with SuperGrok or X Premium+ to use Grok.",
                                        juce::dontSendNotification);
    }
    else
    {
        signInTitle.setText ("Sign in to ChatGPT", juce::dontSendNotification);
        if (! signingIn)
            signInInstructions.setText ("Sign in with your ChatGPT account to use the assistant.",
                                        juce::dontSendNotification);
    }

    signInCard.setVisible (! signedIn);
    grokSignInButton.setVisible (! signedIn && ! signingIn);
    signInButton.setVisible (! signedIn && ! signingIn);
    deviceCodeButton.setVisible (! signedIn && ! selectedGrok && ! signingIn);
    signInButton.setEnabled (! signingIn);
    grokSignInButton.setEnabled (! signingIn);

    if (! signingIn)
    {
        grokPasteEditor.setVisible (false);
        grokPasteButton.setVisible (false);
        grokPasteButton.setEnabled (true);
    }
    const auto reviewing = signedIn && approval != nullptr;

    historyViewport.setVisible (signedIn);
    input.setVisible (signedIn && ! reviewing);
    input.setEnabled (signedIn && ! busy && ! reviewing);

    if (reviewing || busy)
        if (auto* focused = juce::Component::getCurrentlyFocusedComponent())
            focused->giveAwayKeyboardFocus();
    /*  送信と停止は同じ丸ボタン。実行中は停止に変わる。押す場所が動かないので、
        止めたいときに探さずに済む。 */
    sendStopButton->setVisible (signedIn);
    sendStopButton->setIcon (busy ? RoundIconButton::Icon::stop : RoundIconButton::Icon::send);
    sendStopButton->setEnabled (signedIn);

    addFileButton->setVisible (signedIn && ! reviewing);
    permissionButton->setVisible (signedIn && ! reviewing);
    execTargetButton->setVisible (signedIn && ! reviewing);
    modelButton->setVisible (! reviewing);
    updateModelButton();
    updateExecTargetButton();
    approvalCard.setVisible (reviewing);

    if (reviewing)
        approvalCard.toFront (false);

    if (approval != nullptr)
    {
        /*  組み込み git もコマンドとして見せる。ただし下のターミナルへは
            回せない（プロセスの中で動くので）。 */
        const auto isShellCommand = approval->toolName == "exec_command";
        const auto isCommand = isShellCommand || approval->toolName == "git"
                            || approval->toolName == "build";
        approvalTitle.setText (isCommand ? "Review command"
                                         : "Review " + approval->toolName + " change",
                               juce::dontSendNotification);
        approveButton.setButtonText (isShellCommand ? "Run in background"
                                                    : (isCommand ? "Run" : "Apply"));
        runInTerminalButton.setVisible (isShellCommand);
        approvalDiffContent->setText (approval->diffPreview);
    }
    else
    {
        runInTerminalButton.setVisible (false);
    }

    autoApproveToggle.setToggleState (session->getAutoApprove(), juce::dontSendNotification);
    resized();
    repaint();
}

void AiChatView::submitGrokPaste()
{
    if (! grokPasteButton.isVisible() || ! grokPasteButton.isEnabled())
        return;

    const auto pasted = grokPasteEditor.getText().trim();

    if (pasted.isEmpty())
        return;

    grokPasteEditor.giveAwayKeyboardFocus();

    if (! grokAuth->submitPastedInput (pasted))
    {
        signInInstructions.setText ("Sign-in is not waiting for a code. Try Sign in with Grok again.",
                                    juce::dontSendNotification);
        resized();
        return;
    }

    grokPasteButton.setEnabled (false);
    signInInstructions.setText ("Submitting the code...", juce::dontSendNotification);
}

bool AiChatView::keyPressed (const juce::KeyPress& key, juce::Component* origin)
{
    if (origin == &grokPasteEditor
        && key.getKeyCode() == juce::KeyPress::returnKey
        && grokPasteEditor.isVisible())
    {
        submitGrokPaste();
        return true;
    }

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
    if (! isSelectedProviderSignedIn() || session->isBusy())
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
    input.giveAwayKeyboardFocus();
    session->sendMessage (text, attachments);
    updateHistoryLayout (true);
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

    if (command == "/signin" || command == "/login" || command == "/logout")
    {
        restartSignIn();
        return true;
    }

    if (command == "/help")
    {
        session->addLocalNotice ("Commands:\n"
                                 "  /model   choose the model and reasoning effort\n"
                                 "  /signin  sign in again (also /login, /logout)\n"
                                 "  /help    show this list\n"
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
    bool addedChatGptHeader = false;
    bool addedGrokHeader = false;

    for (int i = 0; i < models.size(); ++i)
    {
        const auto& model = models.getReference (i);

        if (model.provider == AiModels::Provider::chatgpt && ! addedChatGptHeader)
        {
            modelMenu.addSectionHeader ("ChatGPT");
            addedChatGptHeader = true;
        }
        else if (model.provider == AiModels::Provider::grok && ! addedGrokHeader)
        {
            modelMenu.addSectionHeader ("Grok");
            addedGrokHeader = true;
        }

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

    if (! efforts.isEmpty())
        menu.addSubMenu ("Reasoning       " + AiModels::labelForEffort (currentEffort), effortMenu);

    if (AiModels::getSelectedProvider() == AiModels::Provider::chatgpt)
        menu.addSubMenu ("Speed           " + AiModels::labelForSpeedTier (currentTier), speedMenu);

    menu.addSeparator();
    menu.addItem (signInAgainId, "Sign in again...");
    menu.addItem (resetId, "Reset to default");

    juce::Component::SafePointer<AiChatView> safeThis (this);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (static_cast<juce::Component*> (modelButton.get())),
                        [safeThis, models, efforts, tiers] (int chosen)
    {
        if (safeThis == nullptr || chosen <= 0)
            return;

        if (chosen == signInAgainId)
        {
            safeThis->restartSignIn();
            return;
        }

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
        safeThis->updateVisibility();
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

    if (AiModels::getSelectedProvider() == AiModels::Provider::chatgpt)
    {
        const auto tier = AiModels::getSelectedSpeedTier();

        if (tier.isNotEmpty())
            detail << " / " << AiModels::labelForSpeedTier (tier);
    }

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

void AiChatView::updateExecTargetButton()
{
    const auto inTerminal = session->getExecDestination()
                            == AiSession::ExecDestination::visibleTerminal;
    execTargetButton->setToggleState (inTerminal, juce::dontSendNotification);
    execTargetButton->setTooltip (inTerminal
        ? "Commands go to the bottom terminal. Click to switch back to background."
        : "Click to type commands into the bottom terminal.");
    resized();
}

void AiChatView::showPermissionMenu()
{
    /*  ChatGPT の「ChatGPT のアクションの承認方法」と同じ 3 段階。
        既定の PopupMenu は 1 行描画なので、タイトルと説明を重ねないよう
        項目ごとに高さを持たせる。 */
    class PermissionChoiceItem final : public juce::PopupMenu::CustomComponent
    {
    public:
        PermissionChoiceItem (juce::String titleToUse, juce::String detailToUse)
            : title (std::move (titleToUse)), detail (std::move (detailToUse))
        {
        }

        void getIdealSize (int& idealWidth, int& idealHeight) override
        {
            idealWidth = 460;
            idealHeight = 58;
        }

        void paint (juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat().reduced (4.0f, 2.0f);
            const auto highlighted = isItemHighlighted();
            const auto ticked = getItem() != nullptr && getItem()->isTicked;
            auto textColour = findColour (highlighted ? juce::PopupMenu::highlightedTextColourId
                                                      : juce::PopupMenu::textColourId);

            if (highlighted)
            {
                g.setColour (findColour (juce::PopupMenu::highlightedBackgroundColourId));
                g.fillRoundedRectangle (bounds, 4.0f);
            }

            auto inner = bounds.reduced (8.0f, 6.0f);
            auto tickArea = inner.removeFromLeft (18.0f);

            if (ticked)
            {
                g.setColour (textColour);
                auto tick = getLookAndFeel().getTickShape (1.0f);
                g.fillPath (tick, tick.getTransformToScaleToFit (tickArea.reduced (2.0f), true));
            }

            auto font = getLookAndFeel().getPopupMenuFont();
            g.setColour (textColour);
            g.setFont (font.boldened());
            g.drawText (title, inner.removeFromTop (18.0f).toNearestInt(),
                        juce::Justification::centredLeft, false);

            g.setColour (textColour.withMultipliedAlpha (0.72f));
            g.setFont (font.withHeight (font.getHeight() * 0.92f));
            g.drawFittedText (detail, inner.toNearestInt(), juce::Justification::topLeft, 2);
        }

    private:
        juce::String title, detail;
    };

    const auto mode = session->getApprovalMode();

    juce::PopupMenu menu;
    menu.addSectionHeader ("How to approve actions");

    const auto addChoice = [&menu, mode] (int id, AiSession::ApprovalMode value,
                                          const juce::String& title, const juce::String& detail)
    {
        juce::ReferenceCountedObjectPtr<juce::PopupMenu::CustomComponent> component (
            new PermissionChoiceItem (title, detail));

        menu.addItem (juce::PopupMenu::Item { title }
                          .setID (id)
                          .setTicked (mode == value)
                          .setCustomComponent (component));
    };

    addChoice (1, AiSession::ApprovalMode::ask,
               "Ask for approval",
               "Always confirm before files are written or commands run");
    addChoice (2, AiSession::ApprovalMode::onUnsafe,
               "Approve on my behalf",
               "Only confirm changes that could lose work, such as creating a file or running a command");
    addChoice (3, AiSession::ApprovalMode::full,
               "Full access",
               "Never confirm, and allow writes and commands without asking");

    juce::Component::SafePointer<AiChatView> safeThis (this);

    menu.showMenuAsync (juce::PopupMenu::Options()
                            .withTargetComponent (static_cast<juce::Component*> (permissionButton.get()))
                            .withMinimumWidth (460)
                            .withPreferredPopupDirection (juce::PopupMenu::Options::PopupDirection::upwards),
                        [safeThis] (int chosen)
    {
        if (safeThis == nullptr || chosen <= 0)
            return;

        const auto chosenMode = chosen == 1 ? AiSession::ApprovalMode::ask
                              : chosen == 2 ? AiSession::ApprovalMode::onUnsafe
                                            : AiSession::ApprovalMode::full;

        safeThis->session->setApprovalMode (chosenMode);
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
        {
            auto controlRow = bounds.removeFromBottom (controlRowHeight);
            const auto modelWidth = juce::jmin (modelButton->getPreferredWidth(), controlRow.getWidth());
            modelButton->setBounds (controlRow.removeFromRight (modelWidth).reduced (0, 3));
        }

        signInCard.setBounds (bounds);

        auto card = signInCard.getLocalBounds().reduced (padding);
        const auto contentWidth = juce::jmin (480, card.getWidth());
        const auto contentHeight = juce::jmin (grokPasteEditor.isVisible() ? 420 : 360, card.getHeight());
        auto content = juce::Rectangle<int> (0, 0, contentWidth, contentHeight);

        if (grokPasteEditor.isVisible())
            content = content.withX (card.getCentreX() - contentWidth / 2).withY (card.getY());
        else
            content = content.withCentre (card.getCentre());

        card = content;

        signInTitle.setBounds (card.removeFromTop (rowHeight));

        if (grokPasteEditor.isVisible())
        {
            grokPasteEditor.setBounds (card.removeFromTop (rowHeight).reduced (0, 2));
            grokPasteButton.setBounds (card.removeFromTop (rowHeight).reduced (0, 2));
        }

        if (signInCode.getText().isNotEmpty())
            signInCode.setBounds (card.removeFromTop (rowHeight + 18));

        if (copyCodeButton.isVisible())
            copyCodeButton.setBounds (card.removeFromTop (rowHeight).reduced (0, 2));

        signInInstructions.setBounds (card.removeFromTop (grokPasteEditor.isVisible() ? rowHeight + 52
                                                                                      : rowHeight + 34));

        if (signInButton.isVisible())
            signInButton.setBounds (card.removeFromTop (rowHeight).reduced (0, 2));

        if (grokSignInButton.isVisible())
            grokSignInButton.setBounds (card.removeFromTop (rowHeight).reduced (0, 2));

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

        const auto execWidth = juce::jmin (execTargetButton->getPreferredWidth(),
                                           controlRow.getWidth());
        execTargetButton->setBounds (controlRow.removeFromLeft (execWidth).reduced (0, 3));
    }

    if (input.isVisible())
        input.setBounds (bounds.removeFromBottom (inputHeight - controlRowHeight + rowHeight).reduced (0, 2));
    else
        input.setBounds ({});

    /*  入力欄と履歴が地続きだと、どこまでが AI の出力か分かりにくい。
        Projucer の地色で帯を挟んで切り分ける。左右はチャット端まで伸ばす。 */
    separatorArea = bounds.removeFromBottom (padding);
    separatorArea.setX (0);
    separatorArea.setWidth (getWidth());

    bounds.removeFromBottom (padding / 2);

    if (approvalCard.isVisible())
    {
        constexpr int actionRowHeight = 44;
        auto approvalHeight = juce::jmin (320, juce::jmax (160, bounds.getHeight() * 2 / 3));
        approvalHeight = juce::jmin (approvalHeight, bounds.getHeight());
        auto cardArea = bounds.removeFromBottom (approvalHeight);
        approvalCard.setBounds (cardArea);

        auto card = approvalCard.getLocalBounds().reduced (4);
        approvalTitle.setBounds (card.removeFromTop (rowHeight));

        auto actionArea = card.removeFromBottom (actionRowHeight);
        const auto applyWidth = runInTerminalButton.isVisible() ? 150 : 96;
        approveButton.setBounds (actionArea.removeFromLeft (applyWidth).reduced (2));

        if (runInTerminalButton.isVisible())
            runInTerminalButton.setBounds (actionArea.removeFromLeft (140).reduced (2));

        rejectButton.setBounds (actionArea.removeFromLeft (96).reduced (2));
        autoApproveToggle.setBounds (actionArea.reduced (2));
        approvalDiffViewport.setBounds (card.reduced (0, 4));

        if (approvalDiffContent != nullptr)
            approvalDiffContent->setSize (juce::jmax (1, approvalDiffViewport.getMaximumVisibleWidth()),
                                          approvalDiffContent->getContentHeight());

        bounds.removeFromBottom (padding / 2);
    }

    historyViewport.setBounds (bounds);
    updateHistoryLayout (false);
}
