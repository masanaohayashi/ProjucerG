/*
    libvterm を1つの翻訳単位にまとめる。

    こうすることで、ビルド定義（CMakeLists.txt と Projucer.jucer の両方）に
    登録するファイルがこの1本だけで済む。同梱した libvterm のソースは
    一切変更しないこと。更新時は libvterm/ を丸ごと差し替える。
*/

#if defined (__clang__)
 #pragma clang diagnostic push
 #pragma clang diagnostic ignored "-Wconversion"
 #pragma clang diagnostic ignored "-Wsign-conversion"
 #pragma clang diagnostic ignored "-Wshorten-64-to-32"
 #pragma clang diagnostic ignored "-Wunused-parameter"
 #pragma clang diagnostic ignored "-Wmissing-prototypes"
#endif

/*
    libvterm/src/ *.c files are normally compiled as independent translation units,
    each with its own private "static" helpers. A handful of those helpers
    collide by name across files once everything lands in a single unity TU:

      - utf8.h (no include guard) is #included by mouse.c, keyboard.c and
        screen.c, each defining its own "static inline utf8_seqlen/fill_utf8".
      - pen.c's setpenattr(), state.c's putglyph()/erase() are unrelated
        functions that happen to share a name with different, incompatible
        ones in screen.c. Worse, state.c/pen.c also read a *struct field*
        of the same name (e.g. "state->callbacks->putglyph"), so a blind
        object-like #define would rename the field access too and fail to
        compile. Function-like macros only expand where the identifier is
        directly followed by '(', which matches every function definition
        and call but not a bare field access - exactly what's needed here.

    None of these are part of the public API (all are file-local statics),
    so it is safe to rename them per-inclusion with #define/#undef here in
    our own file, without touching the vendored sources at all.
*/

#include "libvterm/src/encoding.c"

#define utf8_seqlen utf8_seqlen_mouse
#define fill_utf8 fill_utf8_mouse
#include "libvterm/src/mouse.c"
#undef utf8_seqlen
#undef fill_utf8

#define utf8_seqlen utf8_seqlen_keyboard
#define fill_utf8 fill_utf8_keyboard
#include "libvterm/src/keyboard.c"
#undef utf8_seqlen
#undef fill_utf8

#include "libvterm/src/parser.c"

#define setpenattr(...) setpenattr_pen(__VA_ARGS__)
#include "libvterm/src/pen.c"
#undef setpenattr

#define utf8_seqlen utf8_seqlen_screen
#define fill_utf8 fill_utf8_screen
#include "libvterm/src/screen.c"
#undef utf8_seqlen
#undef fill_utf8

#define putglyph(...) putglyph_state(__VA_ARGS__)
#define erase(...) erase_state(__VA_ARGS__)
#include "libvterm/src/state.c"
#undef putglyph
#undef erase

#include "libvterm/src/unicode.c"
#include "libvterm/src/vterm.c"

#if defined (__clang__)
 #pragma clang diagnostic pop
#endif
