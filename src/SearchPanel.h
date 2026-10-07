/* Copyright 2022 the SumatraPDF project authors (see AUTHORS file).
   License: GPLv3 */

struct MainWindow;

void CreateSearchPanel(MainWindow* win);
void DestroySearchPanel(MainWindow* win);
void OpenSearchSelectionInSidebar(MainWindow* win, Str engineName, Str url);
void OpenSearchSelectionInPopup(MainWindow* win, Str engineName, Str url);
void OnSearchPopupFrameSize(MainWindow* win, int sizeType);
void CloseAllEdgeSearchProcesses();
void RelayoutSearchPanel(MainWindow* win);
