// The settings window: the settings client of the custom-drawn host (ui/host.h), run on the
// settings thread (settings/window.h); never touches XAML. Edits go through the edit controller
// (settings/edits.h) and its production store.

static constexpr PCWSTR kSettingsUiClass = L"AiQuotaSettingsUi_" WH_MOD_ID;
static constexpr PCWSTR kSettingsUiViewportClass = L"AiQuotaSettingsUiView_" WH_MOD_ID;
static constexpr PCWSTR kSettingsUiPopupClass = L"AiQuotaSettingsUiPopup_" WH_MOD_ID;
// Tooltips hold text a control shows cut (an account's state line, a dropdown's value).
static constexpr PCWSTR kSettingsUiTooltipClass = L"AiQuotaSettingsUiTip_" WH_MOD_ID;
// Posted to run the commit queue outside the handler that queued (focus loss, Enter, a slider's
// release).
static constexpr UINT kSettingsProcessMessage = WM_APP + 40;
// Debounced number box and slider key steps: this timer id plus the field.
static constexpr UINT_PTR kSettingsStepTimer = 0x5E00;
// A notification's rebuild waits for an open dropdown or menu to close (a rebuild closes it).
static constexpr UINT_PTR kSettingsRefreshTimer = 0x5D00;
// Copy version info's button shows whether it copied until this timer ends it.
static constexpr UINT_PTR kSettingsCopiedTimer = 0x5C00;
// The product name, English in every language: the general message boxes' caption, and in the
// window's title.
static constexpr PCWSTR kSettingsProductName = L"Taskbar AI Quota";  // i18n-en

// The window's title in a language.
static std::wstring SettingsWindowTitle(const I18nLocale& locale) {
    return I18nText(locale, TrCtx(L"window-title", L"{app} \u2014 Settings"), {{L"app", kSettingsProductName}});
}

// The settings and usage history windows' backdrop for a material: Windows has none for Tinted
// wallpaper, so they use Mica.
static UiBackdrop MaterialBackdrop(Material material) {
    switch (material) {
        case Material::Acrylic: return UiBackdrop::Acrylic;
        case Material::Mica:
        case Material::TintedWallpaper: return UiBackdrop::Mica;
        case Material::MicaAlt: return UiBackdrop::MicaAlt;
        default: return UiBackdrop::None;
    }
}

struct SettingsUiClient final : UiHostClient {
    // Edits; the account key and draft revision each pending step timer may commit (by field)
    SettingsProductionStore store;
    SettingsEdits edits{store};
    std::map<SettingsField, std::pair<uint64_t, uint64_t>> steps;

    // Window state. The title is in the language it was last set in. Custom poll and retention
    // modes stick once chosen or typed into, so their boxes don't disable themselves when a
    // partial number happens to match a preset. The preview's scroll belongs to the position it
    // was built for. The last built view paints the preview; the color dialog's custom colors last
    // for the window. Copy version info's result shows until kSettingsCopiedTimer.
    I18nLocale titleLocale;
    SettingsUiPage page = SettingsUiPage::Accounts;
    bool pollCustom = false, retentionCustom = false;
    float previewScroll = 0;
    TaskbarPlacement previewPlacement = TaskbarPlacement::BeforeIcons;
    SettingsView view;
    std::array<COLORREF, 16> customColors{};
    std::optional<bool> versionCopied;
    // The regional number format each number box's text was last set in, by its target (account
    // key, field): a draft typed into the box parses in it. The frame's or its text draft's, as
    // built, unless an IME composition kept the box's earlier text (the host sets none meanwhile).
    std::map<std::pair<uint64_t, uint32_t>, I18nNumberFormat> boxNumbers;

    // Accounts: the expanded cards, and the card being dragged: its order meanwhile, where it
    // started, the pointer's offset from its top, where it is drawn and where the first card
    // starts (content DIPs).
    std::vector<uint64_t> expanded;
    struct Drag {
        uint64_t key = 0;
        std::vector<uint64_t> order;
        size_t from = 0;
        float grab = 0, top = 0, origin = 0;
    } drag;

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

    int Ask(UiHost& host, const std::wstring& text, const std::wstring& caption, UINT type) {
        return Modal(host, [&] { return SettingsMessageBoxW(host.main, text.c_str(), caption.c_str(), type); });
    }

    // A dialog's language: the current one as it opens, kept until it closes. Windows' own buttons
    // follow the Windows display language.
    static I18nLocale DialogLocale() { return CurrentLocale(); }

    // The controller's notices, a message box each. Account operations explain their failures;
    // other failed saves get the general message.
    void ShowNotices(UiHost& host, int failures = 0) {
        std::vector<SettingsNotice> notices = std::exchange(edits.notices, {});
        int explained = int(std::count_if(notices.begin(), notices.end(),
                                          [](const SettingsNotice& notice) { return notice.failure; }));
        if (failures > explained) {
            Ask(host, I18nText(DialogLocale(), Tr(L"Could not save settings. The change is kept here with its error; "
                                                  L"change it again to retry.")),
                kSettingsProductName, MB_OK | MB_ICONERROR);
        }
        for (const SettingsNotice& notice : notices) {
            if (Unloading() || host.closing) break;
            I18nLocale locale = DialogLocale();
            Ask(host, SettingsNoticeText(locale, notice.code, notice.reason),
                I18nText(locale, TrCtx(L"dialog-caption", L"Account")),
                MB_OK | (notice.kind == SettingsNoticeKind::Error   ? MB_ICONERROR :
                         notice.kind == SettingsNoticeKind::Warning ? MB_ICONWARNING : MB_ICONINFORMATION));
        }
    }

    // Failed saves keep their drafts with an error; the box says so once.
    void Report(UiHost& host, int failures) {
        UiHostRequestRebuild(host, false);
        ShowNotices(host, failures);
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
        steps.erase(field);
    }

    void KillStepTimers(UiHost& host) {
        while (!steps.empty()) KillStepTimer(host, steps.begin()->first);
    }

    // An account action on its card: blocked by its drafts, the card opens on its label.
    SettingsActionResult AccountAction(UiHost& host, uint64_t key, SettingsField action, int value) {
        KillStepTimers(host);
        SettingsActionResult result = edits.AccountAction(key, action, value);
        if (result == SettingsActionResult::Blocked && !Unloading()) ShowLabel(host, key);
        Report(host, 0);
        return result;
    }

    // Expands an account's card and focuses its label.
    void ShowLabel(UiHost& host, uint64_t key) {
        if (std::find(expanded.begin(), expanded.end(), key) == expanded.end()) expanded.push_back(key);
        UiHostRequestFocus(host, {SettingsAccountControlId(edits.SlotOf(key), SettingsField::AccountLabel), -1},
                           true);
        UiHostRequestRebuild(host, false);
    }

    // Each frame takes the current language and regional number format: its text, numbers, the
    // toolkit's text formats and the title all, and number boxes it sets parse in it. The
    // preview's scroll stays within what the built strip can scroll, and a new position starts it
    // from its home. Account cards follow the projection's order, a dragged card its drag order.
    // The window takes the projected material at once.
    void Build(UiHost& host, D2D1_SIZE_F client, UiFrame& frame) override {
        // The title first: setting it sends messages, which must never see the view half built.
        const I18nLocale locale = CurrentLocale();
        if (titleLocale != locale) {
            SetWindowTextW(host.main, SettingsWindowTitle(locale).c_str());
            titleLocale = locale;
        }
        view = {};
        view.locale = locale;
        host.graphics->locale = view.locale.LocaleName();
        view.page = page;
        view.version = WH_MOD_VERSION;
        view.settings = edits.Projection();
        UiHostSetBackdrop(host, MaterialBackdrop(view.settings.material));
        view.transparencyEffects = host.theme.transparency;
        view.status = edits.baseline.status;
        view.drafts = edits.drafts;
        view.pollCustom = pollCustom;
        view.retentionCustom = retentionCustom;
        view.scale = UiHostScale(host);
        if (std::exchange(previewPlacement, view.settings.placement) != view.settings.placement) previewScroll = 0;
        view.previewScroll = previewScroll;
        view.now = NowUnixMs();
        view.versionCopied = versionCopied;
        for (size_t i = 0; i < view.settings.accounts.size() && i < edits.keys.size(); i++) {
            SettingsViewAccount account;
            account.key = edits.keys[i];
            account.slot = edits.SlotOf(account.key);
            if (i < view.status.accounts.size()) account.status = view.status.accounts[i];
            account.expanded = std::find(expanded.begin(), expanded.end(), account.key) != expanded.end();
            view.accounts.push_back(std::move(account));
        }
        if (drag.key) {
            std::vector<AccountConfig> configs;
            std::vector<SettingsViewAccount> cards;
            for (uint64_t key : drag.order) {
                for (size_t i = 0; i < view.accounts.size(); i++) {
                    if (view.accounts[i].key != key) continue;
                    configs.push_back(view.settings.accounts[i]);
                    cards.push_back(view.accounts[i]);
                }
            }
            // Accounts changed meanwhile: the drag ends where it is (another drag restarts it).
            if (cards.size() == view.accounts.size()) {
                view.settings.accounts = std::move(configs);
                view.accounts = std::move(cards);
                view.dragKey = drag.key;
                view.dragTop = drag.top;
            } else {
                drag = {};
            }
        }
        SettingsUiBuildFrame(*host.graphics, view, client, frame);
        for (const std::vector<UiItem>* items : {&frame.main, &frame.content}) {
            for (const UiItem& item : *items) {
                if (item.kind != UiItemKind::Preview) continue;
                previewScroll = view.previewScroll =
                    std::clamp(previewScroll, float(item.minimum), float(item.maximum));
            }
        }
        for (const UiItem& item : frame.content) {
            if (item.kind != UiItemKind::NumberBox) continue;
            const UiTextBox* box = UiFindTextBox(host.boxes, item.id);
            if (box && box->composing) continue;
            // A text draft's text keeps the format it was typed or stepped in.
            const SettingsDraft* draft = edits.Find(item.action.key, SettingsField(item.action.target));
            boxNumbers[{item.action.key, item.action.target}] = draft && draft->text ? draft->numbers : locale.Numbers();
        }
    }

    void PaintCustom(UiHost& host, ID2D1RenderTarget* target, UiTargetCache& cache, const UiItem& item,
                     const UiPaintState& state) override {
        SettingsPaintPreview(target, cache, *host.graphics, host.theme, view, item, state.pointer);
    }

    void Invoke(UiHost& host, UiAction action) override {
        // A modal disables the window: what arrives meanwhile (the focus loss it causes) is ours,
        // except the end of a slider drag its capture loss cut short: that queues its commit,
        // which runs once the modal returns; and a card drag it cancelled.
        if (Unloading() || closePreparation ||
            (modalDepth && action.kind != UiActionKind::SlideEnd && action.kind != UiActionKind::DragCancel)) {
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
                    case SettingsField::KeepHistory: {
                        // The retention dropdown: days, 0 for Forever, or -1 for Custom, which
                        // drops a failed choice, enables the custom box and moves focus there.
                        // Leaving the committed Forever asks first (pruning is shared and final);
                        // Custom then starts at the most days. A commit drops the custom box's
                        // draft (settings/edits.h).
                        bool forever = edits.baseline.settings.historyRetentionDays <= 0;
                        int days = action.value < 0 ? 3650 : action.value;
                        if (forever && days) {
                            I18nLocale locale = DialogLocale();
                            std::wstring question = I18nText(
                                locale, Tr(L"Stop keeping history forever?\n\nHistory will be kept for {days, plural, "
                                           L"one{# day} other{# days}} and limited to 250,000 samples (about 60 MB) "
                                           L"again. Older samples, and the oldest beyond that limit, will be deleted "
                                           L"permanently, for every Windows user."),
                                {{L"days", days}});
                            if (Ask(host, question, I18nText(locale, TrCtx(L"dialog-caption", L"Keep history")),
                                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES ||
                                Unloading() || closePreparation || host.closing) {
                                break;
                            }
                        }
                        retentionCustom = action.value < 0;
                        if (retentionCustom && !forever) {
                            edits.Revert(0, field);
                            UiHostRequestRebuild(host, false);
                        } else {
                            KillStepTimer(host, SettingsField::RetentionDays);
                            Report(host, edits.Commit(0, field, days));
                        }
                        if (retentionCustom) {
                            UiHostRequestFocus(host, {SettingsControlId(SettingsField::RetentionDays), -1}, true);
                        }
                        break;
                    }
                    case SettingsField::ResetBehavior:
                    case SettingsField::ResetHistory:
                    case SettingsField::ResetLayout:
                    case SettingsField::ResetDisplay: {
                        I18nLocale locale = DialogLocale();
                        std::wstring question = I18nText(
                            locale,
                            field == SettingsField::ResetBehavior ?
                                Tr(L"Reset Behavior to its defaults?\n\nAutomatic language, show on the primary "
                                   L"display only, show details on hover, poll every 10 minutes, do not poll hidden "
                                   L"accounts, threshold notifications at red.") :
                            field == SettingsField::ResetLayout ?
                                Tr(L"Reset Layout to its defaults?\n\nStacked bars 100 x 8 px with labels on the left, "
                                   L"32 px rings, logos and number badges in the taskbar\u2019s text color, percentage "
                                   L"text on hover (never on vertical bars) with adaptive alignment, no amounts, light "
                                   L"grey caret pace markers and the default spacing. Accounts are not changed.") :
                            field == SettingsField::ResetDisplay ?
                                Tr(L"Reset Display to its defaults?\n\nAcrylic material, used bar mode, the default "
                                   L"palette (custom colors reset too) with yellow from 50%, orange from 75% and red "
                                   L"from 90%, stale data marked, additional OpenAI rate limits hidden.") :
                                Tr(L"Reset History to its defaults?\n\nRecording turns on for this Windows user, "
                                   L"history is kept forever and the usage history window opens on 7 days. Saved "
                                   L"history stays."));
                        if (Ask(host, question, I18nText(locale, TrCtx(L"dialog-caption", L"Reset settings")),
                                MB_YESNO | MB_ICONQUESTION) != IDYES ||
                            Unloading() || closePreparation || host.closing) {
                            break;
                        }
                        KillStepTimers(host);
                        if (field == SettingsField::ResetBehavior) pollCustom = false;
                        if (field == SettingsField::ResetHistory) retentionCustom = false;
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
                    case SettingsField::PaceTickColorCustom:
                    case SettingsField::BadgeColorCustom:
                    case SettingsField::UsageColorCustom: {
                        SettingsField target =
                            field == SettingsField::PaceTickColorCustom ? SettingsField::PaceTickColor :
                            field == SettingsField::BadgeColorCustom    ? SettingsField::BadgeColor :
                                SettingsField(int(SettingsField::UsageColorBelowYellow) + std::clamp(action.value, 0, 3));
                        COLORREF initial = COLORREF(SettingsFieldValue(edits.Projection(), target));
                        std::optional<COLORREF> chosen = Modal(host, [&] {
                            return UiChooseColor(host.main, initial, customColors, colorDialog);
                        });
                        if (chosen && !Unloading() && !closePreparation && !host.closing) {
                            Report(host, edits.Commit(0, target, int(*chosen)));
                        }
                        break;
                    }
                    case SettingsField::PreviewScroll:
                        break;
                    case SettingsField::CopyVersionInfo: {
                        // Windows' real build: RtlGetVersion, which compatibility shims leave
                        // alone, with the update revision from the registry.
                        RTL_OSVERSIONINFOW os{};
                        os.dwOSVersionInfoSize = sizeof(os);
                        using RtlGetVersion_t = LONG(WINAPI*)(RTL_OSVERSIONINFOW*);
                        HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
                        auto getVersion = reinterpret_cast<RtlGetVersion_t>(
                            ntdll ? GetProcAddress(ntdll, "RtlGetVersion") : nullptr);
                        if (!getVersion || getVersion(&os) != 0) os = {};
                        DWORD revision = 0, size = sizeof(revision);
                        if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                                         L"UBR", RRF_RT_REG_DWORD, nullptr, &revision, &size) != ERROR_SUCCESS) {
                            revision = 0;
                        }
                        // English in every language: diagnostics for issue reports.
                        wchar_t text[128];
                        swprintf(text, ARRAYSIZE(text), L"Taskbar AI Quota %s\r\nWindows %lu.%lu.%lu.%lu",  // i18n-en
                                 WH_MOD_VERSION, os.dwMajorVersion, os.dwMinorVersion, os.dwBuildNumber, revision);
                        bool copied = false;
                        size_t bytes = (wcslen(text) + 1) * sizeof(wchar_t);
                        if (OpenClipboard(host.main)) {
                            if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
                                if (void* data = GlobalLock(memory)) {
                                    memcpy(data, text, bytes);
                                    GlobalUnlock(memory);
                                    copied = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, memory);
                                }
                                if (!copied) GlobalFree(memory);
                            }
                            CloseClipboard();
                        }
                        versionCopied = copied;
                        SetTimer(host.main, kSettingsCopiedTimer, 2000, nullptr);
                        UiHostRequestRebuild(host, false);
                        break;
                    }
                    case SettingsField::OpenHistoryFolder: {
                        const std::wstring& folder = edits.baseline.status.historyFolder;
                        DWORD attributes = folder.empty() ? INVALID_FILE_ATTRIBUTES :
                                                            GetFileAttributesW(folder.c_str());
                        if (attributes == INVALID_FILE_ATTRIBUTES ||
                            !(attributes & FILE_ATTRIBUTE_DIRECTORY)) {
                            I18nLocale locale = DialogLocale();
                            Ask(host, I18nText(locale, Tr(L"Nothing has been recorded yet. The folder is created with "
                                                          L"the first recorded sample.")),
                                I18nText(locale, TrCtx(L"dialog-caption", L"Usage history")),
                                MB_OK | MB_ICONINFORMATION);
                            break;
                        }
                        OpenUrl(folder.c_str());
                        break;
                    }
                    case SettingsField::AccountAdd:
                        // A provider from the Add account menu (the button itself opens it): the
                        // account exists at once; its card opens on its label, and its sign-in
                        // starts once it is named (CommitLabel).
                        if (action.value < 0 || action.value >= int(std::size(kSettingsProviders))) break;
                        KillStepTimers(host);
                        if (uint64_t key = edits.AddAccount(kSettingsProviders[action.value])) ShowLabel(host, key);
                        Report(host, 0);
                        break;
                    case SettingsField::AccountExpand:
                        // Collapsing ends editing in the card: its text commits as on focus loss.
                        std::erase(expanded, action.key);
                        if (action.value) {
                            expanded.push_back(action.key);
                        } else {
                            bool queued = edits.CommitLabel(action.key, true);
                            for (SettingsField text : {SettingsField::AccountTileText, SettingsField::AccountCreditsMax}) {
                                queued |= edits.RequestCommit(action.key, text, true);
                            }
                            if (queued) PostMessageW(host.main, kSettingsProcessMessage, 0, 0);
                        }
                        UiHostRequestRebuild(host, false);
                        break;
                    case SettingsField::AccountHidden:
                    case SettingsField::AccountSignIn:
                    case SettingsField::AccountSignOut:
                    case SettingsField::AccountDashboard:
                        AccountAction(host, action.key, field, action.value);
                        break;
                    case SettingsField::AccountRemove: {
                        // Its drafts first (an invalid label stops it), then one confirmation
                        // naming the account. Removal deletes its stored sign-in; the checkbox,
                        // offered unless it surely has no recorded history, deletes that too.
                        // Without a task dialog, a message box confirms and history stays.
                        uint64_t key = action.key;
                        KillStepTimers(host);
                        if (edits.FlushAccount(key) != SettingsActionResult::Done) {
                            ShowLabel(host, key);
                            Report(host, 0);
                            break;
                        }
                        std::optional<size_t> index = edits.IndexOf(key);
                        if (!index) break;
                        I18nLocale locale = DialogLocale();
                        if (edits.baseline.status.signInRunning) {
                            Ask(host, SettingsNoticeText(locale, SettingsNoticeCode::RemoveSignInRunning),
                                I18nText(locale, TrCtx(L"dialog-caption", L"Account")), MB_OK | MB_ICONINFORMATION);
                            break;
                        }
                        // The question and each conditional paragraph are whole sentences of their own.
                        const AccountConfig& account = edits.baseline.settings.accounts[*index];
                        std::wstring title = I18nText(locale, Tr(L"Remove \"{label}\" ({provider})?"),
                                                      {{L"label", account.label},
                                                       {L"provider", ProviderDisplayName(account.provider)}});
                        std::wstring signIn = *index < edits.baseline.status.accounts.size() &&
                                                      edits.baseline.status.accounts[*index].signedIn ?
                                                  I18nText(locale, Tr(L"Its stored sign-in will be deleted.")) :
                                                  std::wstring();
                        bool history = account.historyId && HistoryMayHaveSamples(account.historyId);
                        std::wstring caption = I18nText(locale, TrCtx(L"dialog-caption", L"Remove account"));
                        std::wstring removeLabel = I18nText(locale, TrCtx(L"dialog-button", L"Remove"));
                        std::wstring alsoHistory = I18nText(locale, Tr(L"Also delete its usage history"));
                        TASKDIALOG_BUTTON remove{IDOK, removeLabel.c_str()};
                        TASKDIALOGCONFIG config{};
                        config.cbSize = sizeof(config);
                        config.hwndParent = host.main;
                        config.dwFlags = TDF_POSITION_RELATIVE_TO_WINDOW;
                        config.dwCommonButtons = TDCBF_CANCEL_BUTTON;
                        config.pszWindowTitle = caption.c_str();
                        config.pszMainIcon = TD_WARNING_ICON;
                        config.pszMainInstruction = title.c_str();
                        config.pszContent = signIn.empty() ? nullptr : signIn.c_str();
                        config.cButtons = 1;
                        config.pButtons = &remove;
                        config.nDefaultButton = IDOK;
                        config.pszVerificationText = history ? alsoHistory.c_str() : nullptr;
                        BOOL deleteHistory = FALSE;
                        int answer = Modal(host, [&] { return SettingsTaskDialog(config, &deleteHistory); });
                        if (!answer) {
                            deleteHistory = FALSE;
                            std::wstring text = title;
                            if (!signIn.empty()) text += L"\n\n" + signIn;
                            if (history) text += L"\n\n" + I18nText(locale, Tr(L"Its usage history is kept."));
                            answer = Ask(host, text, caption, MB_OKCANCEL | MB_ICONWARNING);
                        }
                        if (answer != IDOK || Unloading() || closePreparation || host.closing) break;
                        std::erase(expanded, key);
                        AccountAction(host, key, SettingsField::AccountRemove, history && deleteHistory);
                        break;
                    }
                    default:
                        // Toggles, dropdowns, chips, gallery cards and swatches: the desired
                        // value, committed now.
                        Report(host, edits.Commit(action.key, field, action.value));
                        break;
                }
                break;
            case UiActionKind::TextChange: {
                // Typing supersedes a pending step: its timer no longer commits.
                KillStepTimer(host, field);
                if (field == SettingsField::PollMinutes) pollCustom = true;
                if (field == SettingsField::RetentionDays) retentionCustom = true;
                auto shown = boxNumbers.find({action.key, action.target});
                edits.SetText(action.key, field, std::move(action.text),
                              shown == boxNumbers.end() ? view.locale :
                                                          I18nLocale(view.locale.Language(), shown->second));
                UiHostRequestRebuild(host, false);
                break;
            }
            case UiActionKind::TextCommit:
            case UiActionKind::TextBlur: {
                // Committed from the queue, never inside the focus-loss handler; so is the sign-in
                // of an added account its naming starts.
                KillStepTimer(host, field);
                bool blur = action.kind == UiActionKind::TextBlur;
                if (field == SettingsField::AccountLabel ? edits.CommitLabel(action.key, blur) :
                                                           edits.RequestCommit(action.key, field, blur)) {
                    PostMessageW(host.main, kSettingsProcessMessage, 0, 0);
                }
                UiHostRequestRebuild(host, false);
                break;
            }
            case UiActionKind::TextRevert:
                KillStepTimer(host, field);
                edits.Revert(action.key, field);
                UiHostRequestRebuild(host, false);
                break;
            case UiActionKind::Step: {
                // Alt+Up/Down on an account card moves it by one; focus stays on it.
                if (field == SettingsField::AccountMove) {
                    std::optional<size_t> index = edits.IndexOf(action.key);
                    int to = index ? int(*index) + action.value : -1;
                    if (drag.key || to < 0 || to >= int(edits.keys.size())) break;
                    if (AccountAction(host, action.key, field, to) != SettingsActionResult::Blocked) {
                        UiHostRequestFocus(host, {SettingsAccountControlId(edits.SlotOf(action.key),
                                                                           SettingsField::AccountExpand), -1},
                                           true);
                    }
                    break;
                }
                if (field == SettingsField::PollMinutes) pollCustom = true;
                if (field == SettingsField::RetentionDays) retentionCustom = true;
                steps[field] = {action.key, edits.Step(action.key, field, action.value, view.locale)};
                SetTimer(host.main, kSettingsStepTimer + UINT_PTR(field), 250, nullptr);
                UiHostRequestRebuild(host, false);
                break;
            }
            case UiActionKind::Slide: {
                // A drag only drafts: the preview follows, the release commits. The page rebuilds
                // once per frame; the main window repaints only while the preview is pinned there.
                KillStepTimer(host, field);
                edits.SetValue(action.key, field, action.value);
                UiSurfaceId surface = UiSurfaceId::Content;
                bool pinned = UiFindItem(host.frame, SettingsControlId(SettingsField::PreviewScroll), &surface) &&
                              surface == UiSurfaceId::Main;
                UiHostRequestLiveRebuild(host, pinned);
                break;
            }
            case UiActionKind::SlideEnd:
                KillStepTimer(host, field);
                if (edits.RequestCommit(action.key, field)) {
                    PostMessageW(host.main, kSettingsProcessMessage, 0, 0);
                }
                UiHostRequestRebuild(host, false);
                break;
            case UiActionKind::Pan: {
                // Repaints the strip's surface with the new scroll; the next build keeps it in range.
                UiSurfaceId surface = UiSurfaceId::Main;
                const UiItem* strip =
                    UiFindItem(host.frame, SettingsControlId(SettingsField::PreviewScroll), &surface);
                float minimum = strip ? float(strip->minimum) : 0, maximum = strip ? float(strip->maximum) : 0;
                previewScroll = view.previewScroll =
                    std::clamp(previewScroll + float(action.value), minimum, maximum);
                InvalidateRect(surface == UiSurfaceId::Main ? host.main : host.viewport, nullptr, FALSE);
                break;
            }
            case UiActionKind::Drag: {
                // A card dragged by its handle: placed past every other card whose center is
                // above the pointer, drawn where the pointer holds it. The first report rebuilds
                // the page with the card lifted; later ones only move the built cards, so the
                // pointer's report rate never repaints the whole window. Nothing saves until the
                // drop.
                auto panel = [&](uint64_t key) {
                    return SettingsAccountControlId(edits.SlotOf(key), SettingsField::AccountExpand, 1);
                };
                float y = float(action.value);
                bool start = drag.key != action.key;
                if (start) {
                    std::optional<size_t> index = edits.IndexOf(action.key);
                    const UiItem* card = index ? UiFindItem(host.frame, panel(action.key)) : nullptr;
                    const UiItem* first = index ? UiFindItem(host.frame, panel(edits.keys.front())) : nullptr;
                    if (!card || !first) break;
                    drag = {action.key, edits.keys, *index, y - card->rect.top, card->rect.top, first->rect.top};
                }
                std::vector<float> centers;
                for (uint64_t key : drag.order) {
                    const UiItem* card = key == drag.key ? nullptr : UiFindItem(host.frame, panel(key));
                    if (card) centers.push_back((card->rect.top + card->rect.bottom) / 2);
                }
                std::erase(drag.order, drag.key);
                size_t to = std::min(SettingsReorderTarget(centers, y), drag.order.size());
                drag.order.insert(drag.order.begin() + ptrdiff_t(to), drag.key);
                drag.top = std::max(y - drag.grab, 0.0f);
                if (start) {
                    UiHostRequestRebuild(host, false);
                    break;
                }
                std::vector<uint32_t> panels;
                for (uint64_t key : drag.order) panels.push_back(panel(key));
                UiHostMoveContent(host, [&](std::vector<UiItem>& items) {
                    return SettingsMoveAccountCards(items, panels, panel(drag.key), drag.origin, drag.top);
                });
                break;
            }
            case UiActionKind::Drop:
            case UiActionKind::DragCancel: {
                // A drop is one move to where the card was dropped, after the account's drafts
                // commit; a cancel puts it back. Notifications the drag deferred reload now.
                Drag dropped = std::exchange(drag, {});
                UiHostRequestRebuild(host, false);
                if (!modalDepth && std::exchange(deferredRefresh, false)) {
                    PostMessageW(host.main, kSettingsRefreshMessage, 0, 0);
                }
                auto at = std::find(dropped.order.begin(), dropped.order.end(), dropped.key);
                size_t to = size_t(at - dropped.order.begin());
                if (action.kind == UiActionKind::Drop && dropped.key && at != dropped.order.end() &&
                    to != dropped.from) {
                    AccountAction(host, dropped.key, SettingsField::AccountMove, int(to));
                }
                break;
            }
            case UiActionKind::None:
                break;
        }
    }

    bool Message(UiHost& host, UINT message, WPARAM wParam, LPARAM lParam, LRESULT& result) override {
        result = 0;
        switch (message) {
            case kSettingsProcessMessage:
                Process(host);
                return true;
            // Sign-in, visibility, history ID, storage, consent and poll notifications reload the
            // baseline and statuses only; drafts and text boxes stay. An open popup stays open
            // until it closes; a card drag defers them until it ends.
            case kSettingsRefreshMessage:
            case kHistoryConsentChangedMessage:
                if (modalDepth || (drag.key && host.handleDrag)) {
                    deferredRefresh = true;
                    return true;
                }
                edits.Reload();
                if (host.popup.open) {
                    SetTimer(host.main, kSettingsRefreshTimer, 250, nullptr);
                } else {
                    UiHostRequestRebuild(host, false);
                }
                // A vanished account's dropped drafts.
                if (!edits.notices.empty() && !closePreparation) ShowNotices(host);
                return true;
            case kExitVisualTestMessage:
                SetVisualTestMode(false);
                return true;
            // A regional format change: the next frame shows numbers in the new one.
            case WM_SETTINGCHANGE:
                if (lParam && wcscmp(reinterpret_cast<PCWSTR>(lParam), L"intl") == 0) {
                    UiHostRequestRebuild(host, false);
                }
                return false;
            case WM_DISPLAYCHANGE:
                // The display list changed (the taskbar re-injects on its own).
                if (modalDepth || (drag.key && host.handleDrag)) {
                    deferredRefresh = true;
                } else {
                    edits.Reload();
                    UiHostRequestRebuild(host, false);
                }
                return false;
            case WM_TIMER: {
                if (wParam == kSettingsCopiedTimer) {
                    KillTimer(host.main, kSettingsCopiedTimer);
                    versionCopied.reset();
                    UiHostRequestRebuild(host, false);
                    return true;
                }
                if (wParam == kSettingsRefreshTimer) {
                    if (!host.popup.open) {
                        KillTimer(host.main, kSettingsRefreshTimer);
                        UiHostRequestRebuild(host, false);
                    }
                    return true;
                }
                if (wParam < kSettingsStepTimer || wParam > kSettingsStepTimer + 0xFF) return false;
                KillTimer(host.main, wParam);
                SettingsField field = SettingsField(wParam - kSettingsStepTimer);
                auto stepped = steps.find(field);
                auto [key, revision] = stepped == steps.end() ? std::pair<uint64_t, uint64_t>{} : stepped->second;
                steps.erase(field);
                if (!closePreparation && !host.closing && edits.CommitStep(key, field, revision)) {
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
        // What the saves had to say (a sign-in left behind by a rename, why one failed) shows
        // before the window goes or asks.
        ShowNotices(host);
        if (!close) {
            UiHostRequestRebuild(host, false);
            close = Ask(host,
                        I18nText(DialogLocale(), Tr(L"A change couldn't be saved; it is shown with its error.\n\n"
                                                    L"Discard unsaved changes and close the window?")),
                        kSettingsProductName, MB_YESNO | MB_ICONWARNING) == IDYES ||
                    Unloading();
            if (close) edits.Discard();
        }
        closePreparation = false;
        return close;
    }

    void Destroying() override { SetVisualTestMode(false); }

    void Destroyed() override { g_settingsWindow.store(nullptr); }
};
