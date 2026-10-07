# Walkthrough - Persistent Title Bar Home Button

Added a permanent vector Home button to the window title bar right next to the menu (3 horizontal bars) button.

## Changes

### SvgIcons
- Added vector home icon `gIconHome` in [SvgIcons.h](file:///C:/Users/amitp/AndroidStudioProjects/sumatrapdf/src/SvgIcons.h) and [SvgIcons.cpp](file:///C:/Users/amitp/AndroidStudioProjects/sumatrapdf/src/SvgIcons.cpp).

### MainWindow / Caption
- Added `CB_HOME` to `CaptionButtons` in [MainWindow.h](file:///C:/Users/amitp/AndroidStudioProjects/sumatrapdf/src/MainWindow.h).
- Added `CB_HOME` to `captionRow1`, wired up rendering and click handling (`OpenHomeTab(win)`) in [SumatraPDF.cpp](file:///C:/Users/amitp/AndroidStudioProjects/sumatrapdf/src/SumatraPDF.cpp).
- Added `OpenHomeTab` helper function in [Tabs.cpp](file:///C:/Users/amitp/AndroidStudioProjects/sumatrapdf/src/Tabs.cpp).
