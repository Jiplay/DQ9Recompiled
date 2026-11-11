// The main menu's Nintendo WFC row becomes an "Options" row, in the
// language the game shows its text in, and the WFC icon stays hidden.
// Choosing the row opens the runtime menu's settings as pages of the title
// menu itself: one page of categories, then one page of options per
// category. See config/hooks/dq9_arm9_ov004.toml for the guest code and
// HANDOFF_OPTIONS_MENU.md for how the title scene works.
//
// The pages are made of the title scene's own widgets: entry cells drawn in
// its full-screen panel (212), picked through the main menu's grid, and the
// description of the row under the cursor in the main menu's panel (210),
// moved to the bottom of the screen. The
// hook on the row's handler (0x0216D6A0) runs when a row is chosen, when B
// is pressed and when left or right is pressed (the grid sends all three
// to the handler), and calls the game's functions it needs one after the
// other: it returns true to jump to a `bx r12` with lr set to the handler,
// so each call comes back to the hook.
//
// The rows, their values and their translated labels come from
// options_model.h, which the camp menu's Options row shares.
#include <cctype>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "options_model.h"

namespace {

constexpr uint32_t kCpsrZ = 1u << 30;

// ---- Guest layout -------------------------------------------------------

// Widgets (ov23). The scene keeps them in a list at scene+0x118.
constexpr uint32_t kWidgetType = 0x04u;   // u16: 6 panel, 7 grid, 8 cell
constexpr uint32_t kWidgetId = 0x06u;     // u16
constexpr uint32_t kWidgetGroup = 0x08u;  // u16: the arena it lives in
constexpr uint32_t kWidgetFlags = 0x0Cu;  // u8, bit 3 = hidden
constexpr uint32_t kWidgetNext = 0x18u;
constexpr uint32_t kTypePanel = 6u;
constexpr uint32_t kTypeGrid = 7u;
constexpr uint32_t kTypeCell = 8u;
constexpr uint8_t kHidden = 0x08u;

// Entry cell fields (set up by 0x021F89F4).
constexpr uint32_t kCellText = 0x20u;    // string pointer, 0 = use the text id
constexpr uint32_t kCellLayer = 0x34u;   // u16: the panel it is drawn in
constexpr uint32_t kCellTextId = 0x38u;  // u16
constexpr uint32_t kCellX = 0x3Au;       // s16, pixels inside the panel
constexpr uint32_t kCellY = 0x3Cu;       // s16
constexpr uint32_t kCellAction = 0x48u;  // u16: handler run when chosen

// Grid fields (step: 0x021F8E1C).
constexpr uint32_t kGridCells = 0x20u;   // u16 cell ids, [row * cols + col]
constexpr uint32_t kGridRows = 0x24u;    // u16
constexpr uint32_t kGridCols = 0x26u;    // u16
constexpr uint32_t kGridRow = 0x28u;     // u16 cursor
constexpr uint32_t kGridCol = 0x2Au;     // u16
constexpr uint32_t kGridRedraw = 0x38u;  // u32: move the cursor sprite
constexpr uint32_t kGridHandlers = 0x3Cu;  // u16s up to 0x5C: labels/handlers
constexpr uint32_t kGridHandlersEnd = 0x5Cu;
constexpr uint32_t kGridOnB = 0x46u;      // handler on B
constexpr uint32_t kGridOnRight = 0x54u;  // handler on right
constexpr uint32_t kGridOnLeft = 0x56u;   // handler on left
constexpr uint32_t kGridSavedBegin = 0x20u;
constexpr uint32_t kGridSavedEnd = 0x64u;

// Scene (ov11).
constexpr uint32_t kSceneWidgets = 0x118u;
constexpr uint32_t kSceneLabel = 0x10Cu;       // script label to jump to
constexpr uint32_t kSceneActiveGrid = 0x1B0u;  // u16
constexpr uint32_t kSceneHandlers = 0x1B4u;    // table of handlers
// u16: the handler the scene step (0x02184A84) calls every frame while no
// handler is running (one that returned non-zero is, until it returns 0).
constexpr uint32_t kSceneFrameHandler = 0x1BEu;
// lr of the handler when the scene step calls it as the running handler.
constexpr uint32_t kRunningHandlerReturn = 0x02184AA8u;
// The title's idle time (0x021848A8 adds to it every frame without input
// and jumps to label 999, back to the logos, past 0xE10).
constexpr uint32_t kSceneIdleTime = 0x1D0u;
constexpr uint32_t kSceneCursorDirty = 0x1C8u;

// Panel: its window object is at +0x20; +197 bit 0 of that is "open".
constexpr uint32_t kPanelWindowOpen = 0x20u + 197u;

constexpr uint32_t kMainMenuLayer = 210u;
constexpr uint32_t kFileInfoPanel = 211u;
constexpr uint32_t kPagePanel = 212u;  // up to 32 x 24 tiles (opening.stb)
constexpr uint32_t kWfcTextId = 60u;
constexpr uint32_t kCellGroup = 1u;      // the main menu rows' arena (2 KB)
constexpr uint32_t kFileInfoGroup = 2u;
// opening.stb's way back to the main menu from a page on panel 212: it
// resets groups 1 and 2 and runs the main menu's opener (handler 2).
constexpr uint32_t kMainMenuLabel = 301u;
constexpr uint32_t kOptionsHandler = 10u;

// The pad (0x02114E30): +0 held, +2 held on the previous frame.
constexpr uint32_t kPadHeld = 0x02114E30u;
constexpr uint32_t kPadPrevious = 0x02114E32u;
constexpr uint32_t kKeyA = 0x001u, kKeyB = 0x002u;
constexpr uint32_t kKeyRight = 0x010u, kKeyLeft = 0x020u;

// Guest functions.
constexpr uint32_t kFnHandler = 0x0216D6A0u;      // the hooked handler
constexpr uint32_t kFnNewCell = 0x0216AFB0u;      // ov4
constexpr uint32_t kFnPanelClose = 0x021F8120u;   // ov23 (panel)
constexpr uint32_t kFnPanelOpen = 0x021F7EB8u;    // ov23
constexpr uint32_t kFnRemoveGroup = 0x021F6844u;  // ov23 (widgets, group)
constexpr uint32_t kFnFindGroup = 0x021842C8u;    // ov11 (scene, group)
constexpr uint32_t kFnHeapReset = 0x02032698u;    // main (heap)

// Free guest memory: the end of overlay slot 0 (0x021536E0 .. 0x021842A0),
// past overlay 4's image and bss (they end at 0x02171060), so unused while
// overlay 4 runs the menu. Nothing in main or the title's overlays points
// into 0x02171060 .. 0x021842A0.
constexpr uint32_t kRowText = 0x02183E00u;  // kMaxRows strings
constexpr uint32_t kRowTextSize = 0x60u;
constexpr uint32_t kGridArray = 0x02184100u;  // kMaxRows u16
constexpr uint32_t kTextBuffer = 0x02184200u;  // the Options row's string
constexpr uint32_t kTextBufferSize = 0xA0u;

constexpr uint32_t kDescText = 0x02183CE0u;  // kDescLines strings

constexpr uint32_t kMaxRows = 8u;
constexpr uint32_t kFirstCell = 110u;  // ids 110..120: unused by opening.stb
// The description: the main menu's panel (210, up to 30 x 11 tiles), reopened
// at the bottom of the screen, with one cell per line.
constexpr uint32_t kDescLines = 3u;
constexpr uint32_t kFirstDescCell = kFirstCell + kMaxRows;
constexpr uint32_t kDescPanel = kMainMenuLayer;
constexpr uint32_t kDescHeight = 6u;  // tiles
constexpr uint32_t kDescTop = 24u - 1u - kDescHeight;
constexpr size_t kDescLineChars = 36;     // the font is proportional
constexpr uint32_t kDescCellTextId = 2u;  // any id but the WFC row's
constexpr int kRowX = 12, kRowY = 6, kRowPitch = 13;
constexpr uint32_t kPageWidth = 30u;  // tiles
constexpr uint32_t kPageVram = 96u;   // as the game opens 212 and 210

// ---- Calling guest code from the hook -----------------------------------

struct GuestCall {
    uint32_t target = 0;  // 0: only run `then`
    std::vector<uint32_t> args;
    std::function<void(uint32_t r0)> then;
};

struct Driver {
    bool active = false;
    bool waiting = false;
    uint32_t saved_lr = 0, saved_sp = 0, call_sp = 0;
    uint32_t result = 0;  // what the handler returns
    std::deque<GuestCall> queue;
    std::function<void(uint32_t)> then;
};

Driver g_driver;

void Call(uint32_t target, std::vector<uint32_t> args,
          std::function<void(uint32_t)> then = nullptr) {
    g_driver.queue.push_back({target, std::move(args), std::move(then)});
}

void Then(std::function<void(uint32_t)> fn) {
    g_driver.queue.push_back({0u, {}, std::move(fn)});
}

// ---- The pages ----------------------------------------------------------

using namespace dq9::options;

struct Menu : Model {
    bool open = false;
    uint32_t scene = 0;
    uint32_t grid = 0;
    uint32_t described = ~0u;  // the row the description is of
    uint8_t saved_grid[kGridSavedEnd - kGridSavedBegin] = {};
};

Menu g_menu;

uint32_t FindWidget(uint32_t scene, uint32_t id) {
    for (uint32_t w = bus_read_u32(scene + kSceneWidgets); w;
         w = bus_read_u32(w + kWidgetNext)) {
        if (bus_read_u16(w + kWidgetId) == id) return w;
    }
    return 0;
}

uint32_t FindTyped(uint32_t scene, uint32_t id, uint32_t type) {
    const uint32_t w = FindWidget(scene, id);
    return w && bus_read_u16(w + kWidgetType) == type ? w : 0;
}

uint32_t PageHeight(uint32_t rows) {
    return (kRowY + kRowPitch * (rows ? rows : 1) + 9 + 7) / 8;
}

// Writes the description of the row under the cursor into its cells and
// reopens its panel, which draws them. Its tiles follow the page's.
void ShowDescription() {
    const uint32_t scene = g_menu.scene;
    const uint32_t index = bus_read_u16(g_menu.grid + kGridRow);
    g_menu.described = index;
    std::vector<std::string> lines;
    if (index < g_menu.rows.size())
        lines = Wrap(DescriptionText(g_menu.rows[index]), kDescLineChars,
                     kDescLines);
    for (uint32_t i = 0; i < kDescLines; ++i) {
        const uint32_t cell = FindTyped(scene, kFirstDescCell + i, kTypeCell);
        if (!cell) continue;
        const uint32_t text = kDescText + i * kRowTextSize;
        WriteString(text, kRowTextSize, i < lines.size() ? lines[i] : "");
        bus_write_u32(cell + kCellText, text);
        const uint8_t flags = bus_read_u8(cell + kWidgetFlags);
        bus_write_u8(cell + kWidgetFlags,
                     i < lines.size() ? flags & ~kHidden : flags | kHidden);
    }
    const uint32_t panel = FindTyped(scene, kDescPanel, kTypePanel);
    if (!panel) return;
    const uint32_t rows = static_cast<uint32_t>(g_menu.rows.size());
    Call(kFnPanelClose, {panel});
    Call(kFnPanelOpen,
         {panel, scene, kPageVram + kPageWidth * PageHeight(rows) * 32u, 1u,
          kDescTop, kPageWidth, kDescHeight, 0u, 1u, 0u});
}

// Lays the page's rows out in the cells and the grid, then reopens the
// panel at the page's height, which draws them.
void ShowPage() {
    const uint32_t scene = g_menu.scene, grid = g_menu.grid;
    BuildRows(g_menu, kMaxRows);
    uint32_t count = 0;
    for (uint32_t i = 0; i < kMaxRows; ++i) {
        const uint32_t cell = FindTyped(scene, kFirstCell + i, kTypeCell);
        if (!cell) continue;  // the arena was full: fewer rows
        if (count < g_menu.rows.size() && count == i) {
            const uint32_t text = kRowText + i * kRowTextSize;
            WriteString(text, kRowTextSize, RowText(g_menu, g_menu.rows[i]));
            bus_write_u32(cell + kCellText, text);
            bus_write_u16(cell + kCellX, kRowX);
            bus_write_u16(cell + kCellY,
                          static_cast<uint16_t>(kRowY + kRowPitch * i));
            bus_write_u16(cell + kCellAction, kOptionsHandler);
            bus_write_u8(cell + kWidgetFlags,
                         bus_read_u8(cell + kWidgetFlags) & ~kHidden);
            bus_write_u16(kGridArray + 2u * i, kFirstCell + i);
            ++count;
        } else {
            bus_write_u8(cell + kWidgetFlags,
                         bus_read_u8(cell + kWidgetFlags) | kHidden);
        }
    }
    if (count < g_menu.rows.size()) {
        // Keep "Back" as the last row that fits.
        if (count) {
            g_menu.rows[count - 1] = Row{};
            WriteString(kRowText + (count - 1) * kRowTextSize, kRowTextSize,
                        RowText(g_menu, g_menu.rows[count - 1]));
        }
        g_menu.rows.resize(count);
    }
    for (uint32_t i = count; i < kMaxRows; ++i)
        bus_write_u16(kGridArray + 2u * i, 0u);

    uint32_t& cursor = g_menu.cursor[g_menu.section];
    if (cursor >= count) cursor = count ? count - 1 : 0;
    bus_write_u32(grid + kGridCells, kGridArray);
    bus_write_u16(grid + kGridRows, static_cast<uint16_t>(count ? count : 1));
    bus_write_u16(grid + kGridCols, 1u);
    bus_write_u16(grid + kGridRow, static_cast<uint16_t>(cursor));
    bus_write_u16(grid + kGridCol, 0u);
    for (uint32_t off = kGridHandlers; off < kGridHandlersEnd; off += 2)
        bus_write_u16(grid + off, 0u);
    bus_write_u16(grid + kGridOnB, kOptionsHandler);
    bus_write_u16(grid + kGridOnLeft, kOptionsHandler);
    bus_write_u16(grid + kGridOnRight, kOptionsHandler);
    bus_write_u32(grid + kGridRedraw, 1u);
    bus_write_u32(scene + kSceneCursorDirty, 1u);

    const uint32_t panel = FindTyped(scene, kPagePanel, kTypePanel);
    if (!panel) return;
    Call(kFnPanelClose, {panel});
    Call(kFnPanelOpen, {panel, scene, kPageVram, 1u, 1u, kPageWidth,
                        PageHeight(count), 0u, 1u, 0u});
    Then([scene](uint32_t) {
        ShowDescription();
        bus_write_u32(scene + kSceneCursorDirty, 1u);
    });
}

bool PageShown(uint32_t scene) {
    const uint32_t panel = FindTyped(scene, kPagePanel, kTypePanel);
    return panel && (bus_read_u8(panel + kPanelWindowOpen) & 1u) &&
           FindTyped(scene, kFirstCell, kTypeCell) &&
           g_menu.grid && g_menu.grid == FindTyped(
               scene, bus_read_u16(scene + kSceneActiveGrid), kTypeGrid) &&
           bus_read_u32(g_menu.grid + kGridCells) == kGridArray;
}

void RestoreGrid() {
    const uint32_t grid = g_menu.grid;
    if (!grid || bus_read_u32(grid + kGridCells) != kGridArray) return;
    for (uint32_t i = 0; i < sizeof(g_menu.saved_grid); ++i)
        bus_write_u8(grid + kGridSavedBegin + i, g_menu.saved_grid[i]);
}

void Open(uint32_t scene) {
    const uint32_t grid = FindTyped(
        scene, bus_read_u16(scene + kSceneActiveGrid), kTypeGrid);
    if (!grid || !FindTyped(scene, kPagePanel, kTypePanel)) return;
    g_menu.open = true;
    g_menu.scene = scene;
    g_menu.grid = grid;
    g_menu.section.clear();
    g_menu.pending.clear();
    for (uint32_t i = 0; i < sizeof(g_menu.saved_grid); ++i)
        g_menu.saved_grid[i] = bus_read_u8(grid + kGridSavedBegin + i);

    // The main menu and the save file's summary make room for the page.
    for (const uint32_t id : {kMainMenuLayer, kFileInfoPanel}) {
        if (const uint32_t panel = FindTyped(scene, id, kTypePanel))
            Call(kFnPanelClose, {panel});
    }
    // Hidden: the summary's cells and icons, and the main menu's own rows,
    // which the description's panel would draw (it is theirs).
    for (uint32_t w = bus_read_u32(scene + kSceneWidgets); w;
         w = bus_read_u32(w + kWidgetNext)) {
        const bool main_row =
            bus_read_u16(w + kWidgetType) == kTypeCell &&
            bus_read_u16(w + kCellLayer) == kMainMenuLayer &&
            bus_read_u16(w + kWidgetId) < kFirstCell;
        if (bus_read_u16(w + kWidgetGroup) == kFileInfoGroup || main_row)
            bus_write_u8(w + kWidgetFlags,
                         bus_read_u8(w + kWidgetFlags) | kHidden);
    }
    for (uint32_t i = 0; i < kDescLines; ++i) {
        if (FindWidget(scene, kFirstDescCell + i)) continue;
        Call(kFnNewCell,
             {scene, kFirstDescCell + i, kCellGroup, kDescPanel,
              kDescCellTextId, static_cast<uint32_t>(kRowX),
              static_cast<uint32_t>(kRowY + kRowPitch * i), 10u, 1u});
    }
    for (uint32_t i = 0; i < kMaxRows; ++i) {
        if (FindWidget(scene, kFirstCell + i)) continue;
        Call(kFnNewCell,
             {scene, kFirstCell + i, kCellGroup, kPagePanel, kWfcTextId,
              static_cast<uint32_t>(kRowX),
              static_cast<uint32_t>(kRowY + kRowPitch * i), 10u, 1u});
    }
    Then([](uint32_t) { ShowPage(); });
}

// Back to the main menu: what opening.stb's command 44 does for the rows'
// and the summary's groups (remove their widgets, reset their arenas), then
// its own label that rebuilds the main menu.
void Close() {
    const uint32_t scene = g_menu.scene;
    RestoreGrid();
    g_menu.open = false;
    for (const uint32_t id : {kPagePanel, kDescPanel}) {
        if (const uint32_t panel = FindTyped(scene, id, kTypePanel))
            Call(kFnPanelClose, {panel});
    }
    for (const uint32_t group : {kCellGroup, kFileInfoGroup}) {
        Call(kFnRemoveGroup, {scene + kSceneWidgets, group});
        Call(kFnFindGroup, {scene, group}, [](uint32_t node) {
            if (node) g_driver.queue.push_front({kFnHeapReset, {node + 4u}, nullptr});
        });
    }
    Then([scene](uint32_t) {
        bus_write_u32(scene + kSceneLabel, kMainMenuLabel);
    });
}

void Event(uint32_t scene) {
    if (g_menu.open && (scene != g_menu.scene || !PageShown(scene))) {
        // The scene left the page on its own (a reset, another screen). If
        // the grid still had the page's rows and handlers, one of those
        // sent this event: give the grid back and do nothing else.
        const bool stale = g_menu.grid &&
            bus_read_u32(g_menu.grid + kGridCells) == kGridArray;
        RestoreGrid();
        g_menu.open = false;
        if (stale) return;
    }
    if (!g_menu.open) {
        Open(scene);
        return;
    }
    const uint32_t held = bus_read_u16(kPadHeld);
    const uint32_t pressed = held & ~bus_read_u16(kPadPrevious);
    const uint32_t index = bus_read_u16(g_menu.grid + kGridRow);
    g_menu.cursor[g_menu.section] = index;
    int direction = 0;
    if (!(pressed & kKeyA)) {
        if (pressed & kKeyB) {
            if (g_menu.section.empty()) {
                Close();
            } else {
                g_menu.section.clear();
                ShowPage();
            }
            return;
        }
        if (held & kKeyLeft) direction = -1;
        else if (held & kKeyRight) direction = 1;
    }
    if (index >= g_menu.rows.size()) return;
    const Row row = g_menu.rows[index];
    switch (row.kind) {
    case Row::Kind::Category:
        if (direction != 0) break;
        g_menu.section = row.section;
        ShowPage();
        break;
    case Row::Kind::Item:
        if (Change(g_menu, row.item, direction)) ShowPage();
        break;
    default:
        if (direction != 0) break;
        if (g_menu.section.empty()) {
            Close();
        } else {
            g_menu.section.clear();
            ShowPage();
        }
        break;
    }
}

// Once per frame while a page is open (the handler is then the scene's
// running handler): follow the cursor with the description, and keep the
// title from going back to the logos while the player reads the options.
void Frame(uint32_t scene) {
    if (!g_menu.open) return;
    if (scene != g_menu.scene || !PageShown(scene)) {
        RestoreGrid();
        g_menu.open = false;
        return;
    }
    bus_write_u32(scene + kSceneIdleTime, 0u);
    if (bus_read_u16(g_menu.grid + kGridRow) != g_menu.described)
        ShowDescription();
    // The scene step skips its every-frame handler while one is running.
    const uint32_t every_frame = bus_read_u16(scene + kSceneFrameHandler);
    const uint32_t table = bus_read_u32(scene + kSceneHandlers);
    if (every_frame && every_frame != kOptionsHandler && table)
        Call(bus_read_u32(table + 4u * every_frame), {scene});
}

}  // namespace

// After 0x021F89F4 set up a main menu entry: point the WFC row at our text.
NDS_TITLE_HOOK void Dq9OptionsRowText(uint32_t* cell) {
    if (bus_read_u16(*cell + kCellLayer) != kMainMenuLayer ||
        bus_read_u16(*cell + kCellTextId) != kWfcTextId) {
        return;
    }
    WriteString(kTextBuffer, kTextBufferSize,
                dq9::Tr("main_menu", "options",
                        bus_read_u8(dq9::kGameTextLanguage)));
    bus_write_u32(*cell + kCellText, kTextBuffer);
}

// Before `bicne`: leave the icon's hidden bit set.
NDS_TITLE_HOOK void Dq9OptionsRowHideIcon(uint32_t* cpsr) { *cpsr |= kCpsrZ; }

// The entry of the WFC row's handler (0x0216D6A0, r0 = the scene), which the
// Options row and the pages' rows and grid run. True: call *r12 (the hook
// jumps to a `bx r12`) with lr = this instruction, so the hook runs again
// when it returns. False: return to the handler's caller, with 1 while a
// page is open, which makes the scene step call the handler every frame
// (Frame), and 0 otherwise. The handler's own code (it started the Nintendo
// WFC setup) never runs.
NDS_TITLE_HOOK bool Dq9OptionsMenu(uint32_t* r0, uint32_t* r1, uint32_t* r2,
                                   uint32_t* r3, uint32_t* r12, uint32_t* sp,
                                   uint32_t* lr) {
    Driver& d = g_driver;
    // A call made below returns here with the stack pointer it was given.
    if (d.active && d.waiting && *sp == d.call_sp) {
        d.waiting = false;
        if (auto then = std::move(d.then)) then(*r0);
    } else {
        d = Driver{};
        d.active = true;
        d.saved_lr = *lr;
        d.saved_sp = *sp;
        const uint32_t scene = *r0;
        if (d.saved_lr == kRunningHandlerReturn)
            Frame(scene);
        else
            Event(scene);
        Then([](uint32_t) { g_driver.result = g_menu.open ? 1u : 0u; });
    }
    while (!d.queue.empty()) {
        GuestCall call = std::move(d.queue.front());
        d.queue.pop_front();
        if (!call.target) {
            if (call.then) call.then(0u);
            continue;
        }
        uint32_t* const regs[] = {r0, r1, r2, r3};
        for (size_t i = 0; i < 4; ++i)
            *regs[i] = i < call.args.size() ? call.args[i] : 0u;
        d.call_sp = (d.saved_sp - 0x40u) & ~7u;
        for (size_t i = 4; i < call.args.size(); ++i)
            bus_write_u32(d.call_sp + 4u * static_cast<uint32_t>(i - 4),
                          call.args[i]);
        *sp = d.call_sp;
        *lr = kFnHandler;
        *r12 = call.target;
        d.then = std::move(call.then);
        d.waiting = true;
        return true;
    }
    *sp = d.saved_sp;
    *lr = d.saved_lr;
    *r0 = d.result;
    d = Driver{};
    return false;
}
