# Add Persistent Title Bar Home Button

Add a vector Home button to the window title bar (caption area) just to the right of the menu/system menu buttons.

## User Review Required

- The home button will be permanently visible in the window title bar.
- Clicking it navigates to the home page / dashboard.

## Proposed Changes

### [SvgIcons]

#### [MODIFY] [SvgIcons.h](file:///C:/Users/amitp/AndroidStudioProjects/sumatrapdf/src/SvgIcons.h)
- Declare `gIconHome`.

#### [MODIFY] [SvgIcons.cpp](file:///C:/Users/amitp/AndroidStudioProjects/sumatrapdf/src/SvgIcons.cpp)
- Define SVG path for `gIconHome`.

### [Main Frame / Caption]

#### [MODIFY] [MainWindow.h](file:///C:/Users/amitp/AndroidStudioProjects/sumatrapdf/src/MainWindow.h)
- Add `CB_HOME` to `CaptionButtons` enum.

#### [MODIFY] [SumatraPDF.cpp](file:///C:/Users/amitp/AndroidStudioProjects/sumatrapdf/src/SumatraPDF.cpp)
- Add `CB_HOME` caption button to `captionRow1` (right of system menu / menu).
- Handle painting and click action for `CB_HOME` to open/navigate to the home page.

## Verification Plan

### Manual Verification
- Launch SumatraPDF with `-for-testing`, verify the home button appears in the title bar next to the menu buttons, and clicking it opens the home page.
