// The rebuilt settings window: the settings client of the custom-drawn host (ui/host.h). Opened
// by the temporary "Settings (new)" taskbar menu item on the settings thread; never touches XAML.
// Edits go through the edit controller (settings/edits.h) and its production store.

static constexpr PCWSTR kSettingsUiClass = L"AiQuotaSettingsUi_" WH_MOD_ID;
static constexpr PCWSTR kSettingsUiViewportClass = L"AiQuotaSettingsUiView_" WH_MOD_ID;
static constexpr PCWSTR kSettingsUiPopupClass = L"AiQuotaSettingsUiPopup_" WH_MOD_ID;
// Posted to run the commit queue outside the handler that queued (focus loss, Enter, a slider's
// release).
static constexpr UINT kSettingsProcessMessage = WM_APP + 40;
// Debounced number box and slider key steps: this timer id plus the field.
static constexpr UINT_PTR kSettingsStepTimer = 0x5E00;

struct SettingsUiClient final : UiHostClient {
    // Edits; the draft revision each pending step timer may commit (by field)
    SettingsProductionStore store;
    SettingsEdits edits{store};
    std::map<SettingsField, uint64_t> stepRevisions;

    // Window state. Custom poll mode sticks once chosen or typed into, so the box doesn't
    // disable itself when a partial number happens to match a preset. The last built view
    // paints the preview; the color dialog's custom colors last for the window.
    SettingsUiPage page = SettingsUiPage::Accounts;
    bool pollCustom = false;
    float previewScroll = 0;
    SettingsView view;
    std::array<COLORREF, 16> customColors{};

    // Reentrancy: messages that arrive inside a modal wait until it returns; close preparation
    // ignores input and drops queue messages (its flush covers them). The color dialog's
    // window, while it runs, for the close to cancel it.
    int modalDepth = 0;
    bool deferredProcess = false, deferredRefresh = false;
    bool closePreparation = false;
    HWND colorDialog = nullptr;

    // A modal owned by the window (message box, color dialog): the popup closes first; queue
    // and refresh messages that arrive meanwhile are posted again once it returns.
    template <class Run> auto Modal(UiHost& host, Run run) {
        UiHostClosePopup(host);
        modalDepth++;
        auto result = run();
        modalDepth--;
        if (!modalDepth && host.main) {
            if (std::exchange(deferredProcess, false)) {
                PostMessageW(host.main, kSettingsProcessMessage, 0, 0);
            }
            if (std::exchange(deferredRefresh, false)) {
                PostMessageW(host.main, kSettingsRefreshMessage, 0, 0);
            }
        }
        return result;
    }

    int Ask(UiHost& host, PCWSTR text, PCWSTR caption, UINT type) {
        return Modal(host, [&] { return SettingsMessageBoxW(host.main, text, caption, type); });
    }

    // Failed saves keep their drafts with an error; the box says so once.
    void Report(UiHost& host, int failures) {
        UiHostRequestRebuild(host, false);
        if (failures) {
            Ask(host, L"Could not save settings. The change is kept here with its error; "
                      L"change it again to retry.",
                L"Taskbar AI Quota Bars", MB_OK | MB_ICONERROR);
        }
    }

    void Process(UiHost& host) {
        if (closePreparation || host.closing) return;
        if (modalDepth) {
            deferredProcess = true;
            return;
        }
        Report(host, edits.Process());
    }

    // A cancelled step timer also forgets its revision: a WM_TIMER already generated then
    // commits nothing.
    void KillStepTimer(UiHost& host, SettingsField field) {
        KillTimer(host.main, kSettingsStepTimer + UINT_PTR(field));
        stepRevisions.erase(field);
    }

    void KillStepTimers(UiHost& host) {
        while (!stepRevisions.empty()) KillStepTimer(host, stepRevisions.begin()->first);
    }

    // The preview's scroll stays within what the built strip can scroll.
    void Build(UiHost& host, D2D1_SIZE_F client, UiFrame& frame) override {
        view = {};
        view.page = page;
        view.version = WH_MOD_VERSION;
        view.settings = edits.Projection();
        view.status = edits.baseline.status;
        view.drafts = edits.drafts;
        view.pollCustom = pollCustom;
        view.scale = UiHostScale(host);
        view.previewScroll = previewScroll;
        view.now = NowUnixMs();
        SettingsUiBuildFrame(*host.graphics, view, client, frame);
        for (const std::vector<UiItem>* items : {&frame.main, &frame.content}) {
            for (const UiItem& item : *items) {
                if (item.kind != UiItemKind::Preview) continue;
                previewScroll = view.previewScroll = std::min(previewScroll, float(item.maximum));
            }
        }
    }

    void PaintCustom(UiHost& host, ID2D1RenderTarget* target, UiTargetCache& cache, const UiItem& item,
                     const UiPaintState& state) override {
        SettingsPaintPreview(target, cache, *host.graphics, host.theme, view, item, state.pointer);
    }

    void Invoke(UiHost& host, UiAction action) override {
        // A modal disables the window: what arrives meanwhile (the focus loss it causes) is ours,
        // except the end of a slider drag its capture loss cut short: that queues its commit,
        // which runs once the modal returns.
        if (Unloading() || closePreparation || (modalDepth && action.kind != UiActionKind::SlideEnd)) {
            return;
        }
        SettingsField field = SettingsField(action.target);
        switch (action.kind) {
            case UiActionKind::Navigate: {
                // Never blocks: valid drafts commit, invalid ones revert, failed ones stay.
                KillStepTimers(host);
                int failures = edits.Navigate();
                page = SettingsUiPage(std::clamp(action.value, 0, int(SettingsUiPage::Count) - 1));
                UiHostRequestRebuild(host, true);
                Report(host, failures);
                break;
            }
            case UiActionKind::OpenUrl:
                OpenUrl(action.text.c_str());
                break;
            case UiActionKind::Command:
                switch (field) {
                    case SettingsField::PollMinutes:
                        // The interval dropdown: a preset, or 0 for Custom, which only enables
                        // the custom box and moves focus there.
                        if (!action.value) {
                            pollCustom = true;
                            UiHostRequestRebuild(host, false);
                            UiHostRequestFocus(host, {SettingsControlId(field), -1}, true);
                            break;
                        }
                        pollCustom = false;
                        KillStepTimer(host, field);
                        Report(host, edits.Commit(0, field, action.value));
                        break;
                    case SettingsField::ResetBehavior:
                    case SettingsField::ResetHistory:
                    case SettingsField::ResetLayout: {
                        std::wstring question =
                            field == SettingsField::ResetBehavior ?
                                L"Reset Behavior to its defaults?\n\nShow on the primary display "
                                L"only, show details on hover, poll every 10 minutes, do not poll "
                                L"hidden accounts, threshold notifications on." :
                            field == SettingsField::ResetLayout ?
                                L"Reset Layout to its defaults?\n\nStacked bars 100 x 8 px with labels "
                                L"on the left, 32 px rings, light grey caret pace markers and the "
                                L"default spacing. Accounts are not changed." :
                                L"Reset History to its defaults?\n\nRecording turns on for this "
                                L"Windows user and history is kept for 90 days. Saved history stays.";
                        // Never shorten retention without saying so: pruning is shared and final.
                        // The committed value counts, not a draft (which may have failed).
                        int retention = edits.baseline.settings.historyRetentionDays;
                        if (field == SettingsField::ResetHistory &&
                            retention > Settings{}.historyRetentionDays) {
                            question += L"\n\nHistory is kept for " + std::to_wstring(retention) +
                                        L" days now: samples older than 90 days will be deleted "
                                        L"permanently, for every Windows user.";
                        }
                        if (Ask(host, question.c_str(), L"Reset settings",
                                MB_YESNO | MB_ICONQUESTION) != IDYES ||
                            Unloading() || closePreparation || host.closing) {
                            break;
                        }
                        KillStepTimers(host);
                        if (field == SettingsField::ResetBehavior) pollCustom = false;
                        Report(host, edits.Reset(field));
                        break;
                    }
                    case SettingsField::OpenHistory:
                        OpenHistoryWindow();
                        break;
                    case SettingsField::ShowOnTaskbar:
                        // The taskbar shows the sample accounts with the committed settings.
                        SetVisualTestMode(action.value != 0);
                        edits.Reload();
                        UiHostRequestRebuild(host, false);
                        break;
                    case SettingsField::PaceTickColorCustom: {
                        COLORREF initial = COLORREF(SettingsFieldValue(edits.Projection(), SettingsField::PaceTickColor));
                        std::optional<COLORREF> chosen = Modal(host, [&] {
                            return UiChooseColor(host.main, initial, customColors, colorDialog);
                        });
                        if (chosen && !Unloading() && !closePreparation && !host.closing) {
                            Report(host, edits.Commit(0, SettingsField::PaceTickColor, int(*chosen)));
                        }
                        break;
                    }
                    case SettingsField::PreviewScroll:
                        break;
                    case SettingsField::OpenHistoryFolder: {
                        const std::wstring& folder = edits.baseline.status.historyFolder;
                        DWORD attributes = folder.empty() ? INVALID_FILE_ATTRIBUTES :
                                                            GetFileAttributesW(folder.c_str());
                        if (attributes == INVALID_FILE_ATTRIBUTES ||
                            !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                            Ask(host, L"Nothing has been recorded yet. The folder is created with "
                                      L"the first recorded sample.",
                                L"Usage history", MB_OK | MB_ICONINFORMATION);
                            break;
                        }
                        OpenUrl(folder.c_str());
                        break;
                    }
                    default:
                        // Toggles and the display dropdown: the desired value, committed now.
                        Report(host, edits.Commit(0, field, action.value));
                        break;
                }
                break;
            case UiActionKind::TextChange:
                // Typing supersedes a pending step: its timer no longer commits.
                KillStepTimer(host, field);
                if (field == SettingsField::PollMinutes) pollCustom = true;
                edits.SetText(action.key, field, std::move(action.text));
                UiHostRequestRebuild(host, false);
                break;
            case UiActionKind::TextCommit:
            case UiActionKind::TextBlur:
                // Committed from the queue, never inside the focus-loss handler.
                KillStepTimer(host, field);
                if (edits.RequestCommit(action.key, field, action.kind == UiActionKind::TextBlur)) {
                    PostMessageW(host.main, kSettingsProcessMessage, 0, 0);
                }
                UiHostRequestRebuild(host, false);
                break;
            case UiActionKind::TextRevert:
                KillStepTimer(host, field);
                edits.Revert(action.key, field);
                UiHostRequestRebuild(host, false);
                break;
            case UiActionKind::Step:
                if (field == SettingsField::PollMinutes) pollCustom = true;
                stepRevisions[field] = edits.Step(action.key, field, action.value);
                SetTimer(host.main, kSettingsStepTimer + UINT_PTR(field), 250, nullptr);
                UiHostRequestRebuild(host, false);
                break;
            case UiActionKind::Slide:
                // A drag only drafts: the preview follows; the release commits.
                KillStepTimer(host, field);
                edits.SetValue(action.key, field, action.value);
                UiHostRequestRebuild(host, false);
                break;
            case UiActionKind::SlideEnd:
                KillStepTimer(host, field);
                if (edits.RequestCommit(action.key, field)) {
                    PostMessageW(host.main, kSettingsProcessMessage, 0, 0);
                }
                UiHostRequestRebuild(host, false);
                break;
            case UiActionKind::Pan: {
                // Repaints with the new scroll; the next build keeps it in range.
                const UiItem* strip = UiFindItem(host.frame, SettingsControlId(SettingsField::PreviewScroll));
                float maximum = strip ? float(strip->maximum) : 0;
                previewScroll = view.previewScroll =
                    std::clamp(previewScroll + float(action.value), 0.0f, maximum);
                UiHostInvalidate(host);
                break;
            }
            case UiActionKind::None:
                break;
        }
    }

    bool Message(UiHost& host, UINT message, WPARAM wParam, LPARAM, LRESULT& result) override {
        result = 0;
        switch (message) {
            case kSettingsProcessMessage:
                Process(host);
                return true;
            // Sign-in, visibility, history ID, storage and consent notifications reload the
            // baseline and statuses only; drafts and text boxes stay.
            case kSettingsRefreshMessage:
            case kHistoryConsentChangedMessage:
                if (modalDepth) {
                    deferredRefresh = true;
                    return true;
                }
                edits.Reload();
                UiHostRequestRebuild(host, false);
                return true;
            case kExitVisualTestMessage:
                SetVisualTestMode(false);
                return true;
            case WM_DISPLAYCHANGE:
                // The display list changed (the taskbar re-injects on its own).
                if (modalDepth) {
                    deferredRefresh = true;
                } else {
                    edits.Reload();
                    UiHostRequestRebuild(host, false);
                }
                return false;
            case WM_TIMER: {
                if (wParam < kSettingsStepTimer || wParam > kSettingsStepTimer + 0xFF) return false;
                KillTimer(host.main, wParam);
                SettingsField field = SettingsField(wParam - kSettingsStepTimer);
                auto stepped = stepRevisions.find(field);
                uint64_t revision = stepped == stepRevisions.end() ? 0 : stepped->second;
                stepRevisions.erase(field);
                if (!closePreparation && !host.closing && edits.CommitStep(0, field, revision)) {
                    Process(host);
                }
                UiHostRequestRebuild(host, false);
                return true;
            }
        }
        return false;
    }

    bool Attach(UiHost& host) override {
        edits.Reload();
        g_settingsWindow.store(host.main);
        return true;
    }

    bool CancelModal() override {
        if (colorDialog && IsWindow(colorDialog)) {
            PostMessageW(colorDialog, WM_COMMAND, IDCANCEL, 0);
            return true;
        }
        if (!g_settingsMessageBoxContext || !IsWindow(g_settingsMessageBoxContext->hWnd)) {
            return false;
        }
        g_settingsMessageBoxContext->forced = true;
        PostMessageW(g_settingsMessageBoxContext->hWnd, WM_COMMAND,
                     g_settingsMessageBoxContext->forcedResult, 0);
        return true;
    }

    bool Unloading() override { return g_unloading || g_settingsWindowCancelRequested; }

    // Deliberate close: every draft commits first. One that is invalid or fails keeps the
    // window open with its error unless the user discards it.
    bool PrepareClose(UiHost& host) override {
        if (closePreparation) return false;
        closePreparation = true;
        KillStepTimers(host);
        SettingsCloseResult result = edits.PrepareClose();
        bool close = result != SettingsCloseResult::Blocked;
        if (!close) {
            UiHostRequestRebuild(host, false);
            close = Ask(host,
                        L"A change couldn't be saved; it is shown with its error.\n\n"
                        L"Discard unsaved changes and close the window?",
                        L"Taskbar AI Quota Bars", MB_YESNO | MB_ICONWARNING) == IDYES ||
                    Unloading();
            if (close) edits.Discard();
        }
        closePreparation = false;
        return close;
    }

    void Destroying() override { SetVisualTestMode(false); }

    void Destroyed() override { g_settingsWindow.store(nullptr); }
};
