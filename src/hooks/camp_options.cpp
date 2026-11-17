// The camp menu's Misc. list gets an "Options" row, which opens the same
// pages as the title menu's Options row (options_model.h): the categories,
// then one page of options per category. See
// config/hooks/dq9_arm9_ov002.toml for the guest code.
//
// The pages are shown by the Misc. list itself (camp state and window 0x15,
// step 0x02166168, with its description in window 0x16). While a page is
// open the hooks replace what the list is made of: its entries (0x02156080),
// their text, the window's title and the description. Choosing a row or
// pressing B then goes through the list's own code:
//   - a new page closes the list the way B does (0x02156E90) and sets the
//     camp state back to "open the Misc. list", so the window is opened
//     again at the new page's size, one frame later;
//   - a changed value only has the list window drawn again.
// Left and right step the value under the cursor.
#include <cstdint>
#include <string>

#include "options_model.h"

namespace {

using namespace dq9::options;

// ---- Guest layout -------------------------------------------------------

// The camp menu object (r8 in the Misc. step).
constexpr uint32_t kCampState = 0x1BB8u;     // u32: state = top window id
constexpr uint32_t kCampSubState = 0x1BC0u;  // u32: 0 open, 1 run
constexpr uint32_t kCampMiscCursor = 0x1BFEu;  // s16
constexpr uint32_t kStateMisc = 0x15u;

// The window manager's "next window" fields (camp + 0xEC8), written by
// 0x0215BE00 from the layout table and copied into the window when it opens.
constexpr uint32_t kNextWidth = 0xA0u;   // s16, tiles
constexpr uint32_t kNextHeight = 0xA2u;  // s16, tiles: 3 + 2 per entry
constexpr uint32_t kNextLines = 0xAAu;   // s16: the title and the entries
constexpr uint32_t kLayoutLines = 2u;    // u8 in the layout table's entry
constexpr uint32_t kWindowMisc = 0x15u;

// The Misc. list holds entry codes 0..8 (text 4001 + code, description
// 4020 + code), at most 9 of them; the game itself lists 8 at most.
constexpr uint32_t kMaxEntries = 9u;
constexpr uint32_t kOptionsCode = 9u;   // ours, after the game's
constexpr uint32_t kPageCode = 0x100u;  // + the row, while a page is open

// The pad (0x02114E30): +0 held, +2 held on the previous frame.
constexpr uint32_t kPadHeld = 0x02114E30u;
constexpr uint32_t kPadPrevious = 0x02114E32u;
constexpr uint32_t kKeyRight = 0x010u, kKeyLeft = 0x020u;

// Free guest memory: the end of overlay slot 0 (0x021536E0 .. 0x021842A0),
// far past overlay 2's image and bss (they end at 0x0216D3C0).
constexpr uint32_t kRowText = 0x02183A00u;  // kMaxEntries strings
constexpr uint32_t kRowTextSize = 0x60u;
constexpr uint32_t kTitleText = kRowText + kMaxEntries * kRowTextSize;
constexpr uint32_t kTitleTextSize = 0x40u;
constexpr uint32_t kDescText = kTitleText + kTitleTextSize;
constexpr uint32_t kDescTextSize = 0x100u;

// A page's window is wider than the Misc. list's 12 tiles: its rows are
// "label: value". The description window opens to its right, 12 tiles wide.
constexpr uint32_t kPageWidth = 16u;

// ---- State --------------------------------------------------------------

struct Camp : Model {
    bool open = false;        // a page is shown instead of the Misc. entries
    uint32_t misc_count = 0;  // the Misc. list's entries, ours included
    int options_index = -1;   // our row in the Misc. list
    // Set by one hook for a later one in the Misc. step:
    bool close_list = false;    // next frame: close the list (as B does)
    bool keep_state = false;    // at the step's end: stay in the Misc. state
    bool reopen = false;        // at the step's end: open the list again
    bool reopening = false;     // the list is being opened by `reopen`
    bool redraw_list = false;   // draw the list window again
    int cursor_on_open = -1;    // where the reopened list puts its cursor
};

Camp g_camp;

int16_t S16(uint32_t value) { return static_cast<int16_t>(value & 0xFFFFu); }

uint32_t Language() { return bus_read_u8(dq9::kGameTextLanguage); }

// The window text is markup: outside a <tag>, a comma and an apostrophe
// are written as the game's own text writes them. The description window
// reads "% s" as a format, so its % is doubled (`escape_percent`); the
// title and rows print a % as is.
std::string Markup(const std::string& text, bool escape_percent = false) {
    std::string out;
    bool tag = false;
    for (const char c : text) {
        if (c == '<') tag = true;
        if (!tag && c == ',') out += "<,>";
        else if (!tag && c == '\'') out += "<1>";
        else if (!tag && escape_percent && c == '%') out += "%%";
        else out += c;
        if (c == '>') tag = false;
    }
    return out;
}

// No Back row: B, or the pen outside the window, goes up one page.
void Rebuild() { BuildRows(g_camp, kMaxEntries, false); }

void ShowPage(const std::string& section) {
    g_camp.open = true;
    g_camp.section = section;
    Rebuild();
    uint32_t& cursor = g_camp.cursor[section];
    if (cursor >= g_camp.rows.size()) cursor = 0;
    g_camp.cursor_on_open = static_cast<int>(cursor);
}

// Up one page: from a category to the categories, from those to Misc.
void GoUp() {
    if (g_camp.section.empty()) {
        g_camp.open = false;
        g_camp.cursor_on_open = g_camp.options_index;
    } else {
        ShowPage("");
    }
}

}  // namespace

// 0x02156154, the end of the Misc. entry list builder: sp = its 9 s16
// codes, r7 = how many. Adds the Options row, or lists the open page's rows.
NDS_TITLE_HOOK void Dq9CampOptionsList(uint32_t* r7, uint32_t* sp) {
    if (g_camp.open) {
        const uint32_t count = static_cast<uint32_t>(g_camp.rows.size());
        for (uint32_t i = 0; i < count && i < kMaxEntries; ++i)
            bus_write_u16(*sp + 2u * i, static_cast<uint16_t>(kPageCode + i));
        *r7 = count < kMaxEntries ? count : kMaxEntries;
        return;
    }
    g_camp.options_index = -1;
    if (*r7 < kMaxEntries) {
        g_camp.options_index = static_cast<int>(*r7);
        bus_write_u16(*sp + 2u * *r7, static_cast<uint16_t>(kOptionsCode));
        *r7 += 1u;
    }
    g_camp.misc_count = *r7;
}

// 0x0215BED8 in 0x0215BE00, after the next window's size is set: r4 = the
// window manager, r6 = the layout entry. The Misc. window is sized for 8
// entries at most (19 tiles, 9 lines); a 9th needs one more of each. A
// page's window is wider.
NDS_TITLE_HOOK void Dq9CampOptionsSize(uint32_t* r4, uint32_t* r6) {
    if (bus_read_u8(*r6) != kWindowMisc) return;
    const uint32_t count = g_camp.open
        ? static_cast<uint32_t>(g_camp.rows.size()) : g_camp.misc_count;
    if (g_camp.open) bus_write_u16(*r4 + kNextWidth, kPageWidth);
    // A page's window is sized from its own rows, so a short one (the
    // categories) keeps the margin under its last row.
    if (count > 8u || (g_camp.open && count > 0u)) {
        // One more tile on a page: the margin under the last row.
        bus_write_u16(*r4 + kNextHeight, static_cast<uint16_t>(
            3u + 2u * count + (g_camp.open ? 1u : 0u)));
        bus_write_u16(*r4 + kNextLines, static_cast<uint16_t>(
            static_cast<int>(bus_read_u8(*r6 + kLayoutLines)) +
            static_cast<int>(count) - 7));
    }
}

// 0x0215F53C in the list window's builder, r0 = its title ("Misc."): a
// page shows its own.
NDS_TITLE_HOOK void Dq9CampOptionsTitle(uint32_t* r0) {
    if (!g_camp.open) return;
    const std::string title = g_camp.section.empty()
        ? std::string(dq9::Tr("main_menu", "options", Language()))
        : Text("cat_" + Slug(g_camp.section), g_camp.section);
    WriteString(kTitleText, kTitleTextSize, Markup(title));
    *r0 = kTitleText;
}

// 0x0215F5E4 in the list window's builder, r0 = the text of entry r7, whose
// code is r8 (null for ours: the game has no such text).
NDS_TITLE_HOOK void Dq9CampOptionsLabel(uint32_t* r0, uint32_t* r7,
                                        uint32_t* r8) {
    const uint32_t index = *r7;
    if (index >= kMaxEntries) return;
    std::string text;
    if (g_camp.open) {
        if (index < g_camp.rows.size())
            text = RowText(g_camp, g_camp.rows[index]);
    } else if (S16(*r8) == static_cast<int16_t>(kOptionsCode)) {
        text = dq9::Tr("main_menu", "options", Language());
    } else {
        return;
    }
    const uint32_t address = kRowText + index * kRowTextSize;
    WriteString(address, kRowTextSize, Markup(text));
    *r0 = address;
}

// 0x0215F690 in the description window's builder, r0 = the description of
// the entry under the cursor, r5 = the camp menu.
NDS_TITLE_HOOK void Dq9CampOptionsDescription(uint32_t* r0, uint32_t* r5) {
    const int cursor = S16(bus_read_u16(*r5 + kCampMiscCursor));
    std::string text;
    if (g_camp.open) {
        if (cursor >= 0 && static_cast<size_t>(cursor) < g_camp.rows.size())
            text = DescriptionText(g_camp.rows[static_cast<size_t>(cursor)]);
    } else if (cursor >= 0 && cursor == g_camp.options_index) {
        text = dq9::Tr("camp_menu", "options_description", Language());
    } else {
        return;
    }
    WriteString(kDescText, kDescTextSize, Markup(text, true));
    *r0 = kDescText;
}

// 0x021661B0, the Misc. state opening its windows (sub-state 0), after it
// put the cursor on the first entry: r8 = the camp menu. Opened by the
// hooks, the cursor goes where the page (or Misc.) had it; opened by the
// game, the list is the Misc. list again.
NDS_TITLE_HOOK void Dq9CampOptionsOpen(uint32_t* r8) {
    if (!g_camp.reopening) {
        g_camp = Camp{};
        return;
    }
    g_camp.reopening = false;
    if (g_camp.cursor_on_open >= 0)
        bus_write_u16(*r8 + kCampMiscCursor,
                      static_cast<uint16_t>(g_camp.cursor_on_open));
    g_camp.cursor_on_open = -1;
}

// 0x021661E0, the start of the Misc. state's every-frame part (sub-state
// 1): r8 = the camp menu. True: jump to the code that closes the list
// (0x02166588), for a new page. Otherwise left and right step the value
// under the cursor.
NDS_TITLE_HOOK bool Dq9CampOptionsFrame(uint32_t* r8) {
    if (g_camp.close_list) {
        g_camp.close_list = false;
        g_camp.reopen = true;
        return true;
    }
    if (!g_camp.open) return false;
    const uint32_t held = bus_read_u16(kPadHeld);
    const uint32_t pressed = held & ~bus_read_u16(kPadPrevious);
    const int direction = (pressed & kKeyLeft) ? -1
                        : (pressed & kKeyRight) ? 1 : 0;
    const int cursor = S16(bus_read_u16(*r8 + kCampMiscCursor));
    if (direction == 0 || cursor < 0 ||
        static_cast<size_t>(cursor) >= g_camp.rows.size())
        return false;
    const Row row = g_camp.rows[static_cast<size_t>(cursor)];
    if (row.kind == Row::Kind::Item && Change(g_camp, row.item, direction)) {
        Rebuild();
        g_camp.redraw_list = true;
    }
    return false;
}

// 0x0216627C, `cmp r0, #0`: r0 != 0 draws the list window again.
NDS_TITLE_HOOK void Dq9CampOptionsRedraw(uint32_t* r0) {
    if (!g_camp.redraw_list) return;
    g_camp.redraw_list = false;
    *r0 = 1u;
}

// 0x02166344, `cmp r0, #8` on the code of the chosen entry (A or the pen).
// True for our entries: jump to 0x021663B4, which plays the choice sound
// and sets a camp state that Dq9CampOptionsExit then puts back.
NDS_TITLE_HOOK bool Dq9CampOptionsChoose(uint32_t* r0) {
    const int code = S16(*r0);
    if (!g_camp.open) {
        if (code != static_cast<int>(kOptionsCode)) return false;
        g_camp.pending.clear();
        ShowPage("");
        g_camp.close_list = true;
        g_camp.keep_state = true;
        return true;
    }
    const int index = code - static_cast<int>(kPageCode);
    if (index < 0 || static_cast<size_t>(index) >= g_camp.rows.size())
        return false;
    g_camp.cursor[g_camp.section] = static_cast<uint32_t>(index);
    const Row row = g_camp.rows[static_cast<size_t>(index)];
    switch (row.kind) {
    case Row::Kind::Category:
        ShowPage(row.section);
        g_camp.close_list = true;
        break;
    case Row::Kind::Item:
        if (Change(g_camp, row.item, 0)) {
            Rebuild();
            g_camp.redraw_list = true;
        }
        break;
    default:
        GoUp();
        g_camp.close_list = true;
        break;
    }
    g_camp.keep_state = true;
    return true;
}

// 0x02166580, `cmp r0, #0`: r0 != 0 when B (or the pen's cancel) closes the
// list. On a page that goes up one page: the list closes as usual and
// Dq9CampOptionsExit has it opened again.
NDS_TITLE_HOOK void Dq9CampOptionsBack(uint32_t* r0, uint32_t* r8) {
    if (!g_camp.open || *r0 == 0u) return;
    g_camp.cursor[g_camp.section] = static_cast<uint32_t>(
        S16(bus_read_u16(*r8 + kCampMiscCursor)) & 0x7FFF);
    GoUp();
    g_camp.reopen = true;
}

// 0x0216659C, the end of the Misc. step: r8 = the camp menu.
NDS_TITLE_HOOK void Dq9CampOptionsExit(uint32_t* r8) {
    if (g_camp.keep_state) {
        g_camp.keep_state = false;
        bus_write_u32(*r8 + kCampState, kStateMisc);
        bus_write_u32(*r8 + kCampSubState, 1u);
    }
    if (g_camp.reopen) {
        g_camp.reopen = false;
        g_camp.reopening = true;
        bus_write_u32(*r8 + kCampState, kStateMisc);
        bus_write_u32(*r8 + kCampSubState, 0u);
    }
}
