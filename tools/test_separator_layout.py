#!/usr/bin/env python3
"""Self-check for the splitter-bar class that the GUI editor's Separator emits.

Extracts JucerSeparatorBar verbatim from the handler, compiles it against minimal
JUCE stubs and asserts the split arithmetic. Run: python3 tools/test_separator_layout.py
"""
import pathlib, subprocess, sys, tempfile

SRC = pathlib.Path(__file__).parent.parent / "Projucer/Source/ComponentEditor/Components"
HANDLER = SRC / "jucer_SeparatorComponentHandler.h"
EDITOR = SRC / "jucer_SeparatorComponent.h"


SHARED = ("int getTravel() const",
          "int getWantedSplitPosition (int travel) const",
          "void setSplitPosition (int pos, int travel)",
          "void applyLayout()")


def body(text, signature):
    """One function body, normalised so the two copies can be compared."""
    chunk = text.split(signature, 1)[1]
    chunk = chunk[: chunk.index("\n    }\n")]
    chunk = chunk.replace("juce::", "").replace("const auto vertical = isVertical();", "")
    return [line.strip() for line in chunk.splitlines() if line.strip()]


def check_copies_in_sync():
    """The editor's SeparatorComponent and the emitted JucerSeparatorBar do the same
    layout maths in two places - fail loudly if one of them is edited alone."""
    editor, handler = EDITOR.read_text(), HANDLER.read_text()

    for signature in SHARED:
        if body(editor, signature) != body(handler, signature):
            raise SystemExit(f"{signature} has drifted between "
                             f"{EDITOR.name} and the class emitted by {HANDLER.name}")

STUBS = r"""
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <cassert>
namespace juce {
template <typename T> T jlimit (T lo, T hi, T v) { return v < lo ? lo : (v > hi ? hi : v); }
inline int roundToInt (double d) { return (int) std::lround (d); }
struct Point { int x, y; };
template <typename T> struct Rectangle {
    T x{}, y{}, w{}, h{};
    Rectangle() = default;
    Rectangle (T x_, T y_, T w_, T h_) : x (x_), y (y_), w (w_), h (h_) {}
    T getX() const { return x; } T getY() const { return y; }
    T getWidth() const { return w; } T getHeight() const { return h; }
    T getRight() const { return x + w; } T getBottom() const { return y + h; }
    Rectangle getUnion (Rectangle o) const {
        auto l = std::min (x, o.x), t = std::min (y, o.y);
        return { l, t, std::max (getRight(), o.getRight()) - l, std::max (getBottom(), o.getBottom()) - t };
    }
};
struct Colour { Colour contrasting (float) const { return *this; } };
struct Graphics { void fillAll (Colour) {} };
struct MouseEvent { int x = 0, y = 0; Point getPosition() const { return { x, y }; } MouseEvent getEventRelativeTo (void*) const { return *this; } };
struct MouseCursor { enum Type { LeftRightResizeCursor, UpDownResizeCursor }; MouseCursor (Type) {} };
struct ResizableWindow { enum { backgroundColourId = 1 }; };
struct Component {
    Rectangle<int> b;
    virtual ~Component() = default;
    void setBounds (int x, int y, int w, int h) { b = { x, y, w, h }; }
    Rectangle<int> getBounds() const { return b; }
    int getX() const { return b.x; } int getY() const { return b.y; }
    int getWidth() const { return b.w; } int getHeight() const { return b.h; }
    Component* getParentComponent() const { return nullptr; }
    void setMouseCursor (MouseCursor) {}
    virtual void paint (Graphics&) {}
    virtual void mouseEnter (const MouseEvent&) {}
    virtual void mouseExit (const MouseEvent&) {}
    virtual void mouseDown (const MouseEvent&) {}
    virtual void mouseDrag (const MouseEvent&) {}
    virtual void parentSizeChanged() {}
    bool isColourSpecified (int) const { return false; }
    Colour findColour (int) const { return {}; }
    bool isMouseOverOrDragging() const { return false; }
    void repaint() {}
    template <typename T> struct SafePointer {
        T* p = nullptr;
        SafePointer() = default;
        SafePointer& operator= (T* q) { p = q; return *this; }
        operator T*() const { return p; }
        T* operator->() const { return p; }
        bool operator== (std::nullptr_t) const { return p == nullptr; }
        bool operator!= (std::nullptr_t) const { return p != nullptr; }
    };
};
} // namespace juce
#define JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(x)
"""

TEST = r"""
int main()
{
    juce::Component a, b;

    JucerSeparatorBar bar (true, JucerSeparatorBar::proportional, 0.5, 0, 50, 60);
    bar.setBounds (196, 0, 8, 100);
    a.setBounds (0, 0, 196, 100);
    b.setBounds (204, 0, 196, 100);
    bar.setTargets (&a, &b);

    bar.setProportion (0.25);
    assert (a.getX() == 0);
    assert (a.getWidth() == 98);
    assert (bar.getX() == 98);
    assert (b.getX() == 106);
    assert (b.getWidth() == 294);
    assert (a.getWidth() + bar.getWidth() + b.getWidth() == 400);
    assert (a.getHeight() == 100 && b.getHeight() == 100);

    bar.setProportion (0.0);
    assert (a.getWidth() == 50);
    assert (b.getWidth() == 400 - 8 - 50);

    bar.setProportion (1.0);
    assert (b.getWidth() == 60);
    assert (a.getWidth() == 400 - 8 - 60);

    juce::Component c, d;
    JucerSeparatorBar hbar (false, JucerSeparatorBar::proportional, 0.5, 0, 0, 0);
    hbar.setBounds (0, 147, 200, 6);
    c.setBounds (0, 0, 200, 147);
    d.setBounds (0, 153, 200, 147);
    hbar.setTargets (&c, &d);

    hbar.setProportion (0.75);
    assert (c.getY() == 0 && c.getHeight() == 221);
    assert (hbar.getY() == 221);
    assert (d.getY() == 227 && d.getHeight() == 73);
    assert (c.getHeight() + hbar.getHeight() + d.getHeight() == 300);
    assert (c.getWidth() == 200 && d.getWidth() == 200);

    JucerSeparatorBar orphan (true, JucerSeparatorBar::proportional, 0.5, 0, 0, 0);
    orphan.setBounds (10, 10, 8, 50);
    orphan.setProportion (0.9);
    assert (orphan.getX() == 10);

    // keepBeforeFixed: growing the parent must leave "before" alone
    {
        juce::Component e, f;
        JucerSeparatorBar fixedBar (true, JucerSeparatorBar::keepBeforeFixed, 0.0, 150, 0, 0);
        e.setBounds (0, 0, 150, 100);
        fixedBar.setBounds (150, 0, 8, 100);
        f.setBounds (158, 0, 242, 100);          // 400 wide in total
        fixedBar.setTargets (&e, &f);

        fixedBar.parentSizeChanged();
        assert (e.getWidth() == 150 && f.getWidth() == 242);

        f.setBounds (158, 0, 442, 100);          // parent grew to 600
        fixedBar.parentSizeChanged();
        assert (e.getWidth() == 150);            // untouched
        assert (fixedBar.getX() == 150);
        assert (f.getWidth() == 600 - 8 - 150);  // absorbed all of it
    }

    // keepAfterFixed: the mirror image
    {
        juce::Component e, f;
        JucerSeparatorBar fixedBar (true, JucerSeparatorBar::keepAfterFixed, 0.0, 242, 0, 0);
        e.setBounds (0, 0, 150, 100);
        fixedBar.setBounds (150, 0, 8, 100);
        f.setBounds (158, 0, 242, 100);
        fixedBar.setTargets (&e, &f);

        fixedBar.parentSizeChanged();
        assert (e.getWidth() == 150 && f.getWidth() == 242);

        f.setBounds (158, 0, 442, 100);          // parent grew to 600
        fixedBar.parentSizeChanged();
        assert (f.getWidth() == 242);            // untouched
        assert (f.getX() == 600 - 242);
        assert (e.getWidth() == 600 - 8 - 242);  // absorbed all of it
    }

    std::puts ("separator layout: all assertions passed");
    return 0;
}
"""

def main():
    check_copies_in_sync()

    emitted = HANDLER.read_text().split('return R"(', 1)[1].rsplit(')";', 1)[0]

    with tempfile.TemporaryDirectory() as d:
        src, exe = pathlib.Path(d) / "t.cpp", pathlib.Path(d) / "t"
        src.write_text(STUBS + emitted + TEST)
        subprocess.run(["c++", "-std=c++17", "-o", str(exe), str(src)], check=True)
        subprocess.run([str(exe)], check=True)

if __name__ == "__main__":
    sys.exit(main())
