# Theme: colours, fonts and icons

Meow2Renderer (Dear ImGui) and Meow2Editor (Qt) share one look: **high contrast, pure black, one red accent**, with flat square panels, thin lines and uppercase headings, in the spirit of a tactical HUD.

- **Status:** ✅ (#178). Picked by the owner from mockups: "concept B on pure black", Barlow Semi Condensed with Consolas, outline asset icons, a red slit-eyed cat as the app icon.

## Palette

| Token | Hex | Used for |
|---|---|---|
| base | `#000000` | backgrounds, title bars, the scene's clear colour (near it) |
| panel | `#0C0C0D` | windows, docks, lists, menus (ImGui windows at 94 % opacity over the scene) |
| frame | `#18181B` | fields, buttons, selected tabs |
| frame hovered | `#222226` | a row or field under the mouse |
| line | `#34343A` | borders, separators, scroll bars |
| text | `#E6E6E6` | text: about 17:1 contrast on black |
| text dim | `#8A8A90` | labels, disabled and secondary text (AA contrast, 4.5:1 or better) |
| **accent** | `#E01E2D` | everything active: hovered, pressed, selected, focused, checked; plots, sliders, the text cursor |
| accent bright | `#FF5A66` | red text (errors, hovered tabs, links): readable on black |

Red means "active" and nothing else. Controls stay grey until the mouse is on them; then a red wash, stronger when pressed. Selection is a faint red wash with a red edge. Section headers and dock titles carry a short red bar, and windows red corner brackets.

The values live in two places, because the apps share no UI code: `Meow2Renderer/src/UiTheme.h` (`UiColors`) and `Meow2Editor/src/EditorTheme.h` (`EditorColors`). Change both, and this table.

## Fonts

| Font | Where | Why |
|---|---|---|
| **Barlow Semi Condensed** (Regular, Medium) | labels, buttons, titles, menus | condensed and technical, but readable at small sizes; SIL Open Font License, so it ships in `assets/fonts/` and works the same on every platform |
| **Consolas** | numbers, file paths, log and console output | aligned figures; on Windows it's loaded from the system font folder (`Meow2::systemFontDirectory()`), because its licence doesn't allow shipping it. Elsewhere: ImGui's built-in font (Renderer) or the system's fixed-width font (Editor) |

`meow2_app_fonts(<target>)` copies `assets/fonts/` into `fonts/` next to the executable after every build, and the release zip gets one copy with the licence (`OFL.txt`).

## In Meow2Renderer (ImGui)

`applyUiTheme(style, scale)` sets every size and colour: no rounding, 1-pixel borders, the palette above. It rebuilds the style from ImGui's defaults each time, so a new display scale (a window moved to a 150 % screen) doesn't compound. `drawWindowAccents()` draws the corner brackets and the title tick; each window calls it after `Begin()`.

- Window and section titles are written in capitals (`"STATS###Stats"`: the `###` part keeps the window's ID, and so its position, whatever the title says).
- `ImGuiLayer` adds Barlow first (so it's the default font) and Consolas second; `pushMonoFont(ui.fonts())` switches to Consolas for figures and paths. ImGui 1.92 draws every font at `style.FontSizeBase` (13 px, times the display scale), whatever size it was added at, so Consolas, which looks bigger than Barlow at the same size, is pushed at 85 % of it.
- ImGui uses one colour for selected rows and collapsing headers; the theme makes it red for selection, so headers push grey around themselves.

## In Meow2Editor (Qt)

`applyEditorTheme(app, fontFolder, log)` runs once, right after the `QApplication` is made:

1. **Fusion style**: it draws every widget from the palette, the same way on every platform. The native Windows style ignores much of the palette.
2. **Palette** (`editorPalette()`): for anything the style sheet doesn't cover.
3. **Barlow** registered with `QFontDatabase::addApplicationFont` and set as the application font.
4. **Style sheet** (`editorStyleSheet()`): square corners, thin borders, red focus, hover and selection; dock titles with a red left bar; the selected tab underlined in red; thin scroll bars.

`editorMonospaceFont()` is the Consolas font for the console dock (M7). Dock titles are written in capitals by convention, like the Renderer's.

The editor's window is only the viewport so far, so most of this shows once docks and menus arrive. `Editor.EditorThemeTest.ThemedWidgetsDrawBlackAndRed` builds a stand-in layout off-screen (menu bar, a dock with a selected row, tabs, a field and a button), checks its colours, and saves it as `meow2-editor-theme.png` in the temp folder.

## Icons

### App icons

| File | Executable | Look |
|---|---|---|
| `assets/icons/Meow2.ico` | Meow2Player | the red cat: black outline and inner ears, slit eyes, charcoal plate |
| `assets/icons/Meow2Editor.ico` | Meow2Editor | the cat with a black corner badge, white pixel **E** |
| `assets/icons/Meow2Renderer.ico` | Meow2Renderer | the cat with a black corner badge, white pixel **R** |

- The Player keeps the plain cat: exported games ship it, until a game sets its own.
- Each `.ico` holds nine sizes, 16 to 256 pixels, drawn separately rather than shrunk. Below 32 pixels the outline and eyes are thicker and the inner ears are left out, so the face stays crisp.
- The badge letters are pixel art: 3 × 5 cells below 48 pixels, 5 × 7 above, laid out on whole pixels so they never blur.
- `assets/icons/Meow2.svg` is the cat's master drawing.
- `meow2_app_icon(<target> <icon>)` embeds one through a resource script (`Meow2.rc.in`) named `IDI_ICON1`: Qt looks for that name, and SDL and Explorer take the first icon in the file.

### Asset icons

`assets/icons/assets/*.svg`, 24 × 24 outlines in light grey with one red detail, for the editor's asset browser (M7):

| Icon | Asset |
|---|---|
| `Folder` | folder |
| `Mesh` | mesh: a cube's edges |
| `Texture` | texture: a picture with a sun |
| `Material` | material: a sphere with a highlight |
| `Shader` | shader: a pixel grid with one pixel written |
| `Scene` | scene: mountains |
| `Prefab` | prefab: two stacked squares |
| `Audio` | audio: a speaker |
| `Script` | script: `</>`, kept for scripting |

## Tests

| Test | What it checks |
|---|---|
| `RendererApp.UiTheme.AccentsAreRedOnDark` | near-black panels, light text, the accent on every active colour, no rounding |
| `RendererApp.UiTheme.ScalesSizes` | sizes and fonts follow the display scale, and re-applying doesn't compound |
| `Editor.EditorTheme.PaletteIsDarkWithRedHighlight` | palette colours; text contrast at least 7:1, dimmed text at least 4.5:1 |
| `Editor.EditorTheme.ContrastRatioMatchesWcag` | the contrast formula (black on white is 21:1) |
| `Editor.EditorThemeTest.AppliesToTheApplication` | palette, style sheet and Barlow applied |
| `Editor.EditorThemeTest.MissingFontIsLoggedAndTheRestApplies` | a missing font is logged and the rest still applies |
| `Editor.EditorThemeTest.ThemedWidgetsDrawBlackAndRed` | a themed layout drawn off-screen is black, with a red selected row |
