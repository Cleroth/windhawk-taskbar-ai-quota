// Production store of the settings window's edit controller (settings/edits.h): the published
// settings and statuses, and one transaction per edit. Account edits and actions go through the
// shared account commands (settings/account-commands.h), sign-ins through auth/login.h. Settings
// thread only.

struct SettingsProductionStore final : SettingsStore {
    SettingsBaseline Load() override {
        SettingsBaseline baseline;
        std::vector<AccountData> data;
        {
            std::lock_guard<std::mutex> lock(g_settingsMutex);
            baseline.settings = g_settings;
            std::lock_guard<std::mutex> dataLock(g_dataMutex);
            data = g_data;
        }
        SettingsStatus& status = baseline.status;
        status.recording = g_historyIdsPendingSave ? SettingsRecording::PendingSave :
                           g_historyStorageError   ? SettingsRecording::StorageError :
                                                     SettingsRecording::Recording;
        // Each account's last poll (published in account order) and its sign-in: stored (only
        // its presence is read) and whether its sign-in window is open. OpenAI's Spark plan name
        // shows only with its extra limits, as in the details flyout.
        bool signingIn = g_loginInProgress.load();
        uint64_t signingInIdentity = g_loginAccountIdentity.load();
        status.signInRunning = signingIn;
        for (size_t i = 0; i < baseline.settings.accounts.size(); i++) {
            const AccountConfig& account = baseline.settings.accounts[i];
            uint64_t identity = AccountIdentityHash(account);
            SettingsAccountStatus& accountStatus = status.accounts.emplace_back();
            if (i < data.size()) {
                accountStatus.plan = data[i].plan;
                accountStatus.error = data[i].error;
                accountStatus.polled = data[i].lastSuccessMs != 0;
                accountStatus.needsSignIn = data[i].needsLogin;
            }
            if (account.provider == L"openai" && !baseline.settings.showOpenAiExtraLimits &&
                (accountStatus.plan.find(L"Spark") != std::wstring::npos ||  // i18n-en
                 accountStatus.plan.find(L"spark") != std::wstring::npos)) {
                accountStatus.plan.clear();
            }
            accountStatus.signedIn = account.provider != L"antigravity" && HasStoredToken(identity);
            accountStatus.signingIn = signingIn && signingInIdentity == identity;
        }
        // Display N is the Nth taskbar in the order the taskbar code targets (primary first).
        // The preview takes the primary taskbar's thickness, converted with that taskbar's DPI.
        std::vector<TaskbarDisplayInfo> taskbars = FindCurrentProcessTaskbarDisplays();
        for (size_t i = 0; i < taskbars.size(); i++) {
            const RECT& rect = taskbars[i].rect;
            status.displays.push_back({int(i) + 1, int(std::abs(rect.right - rect.left)),
                                       int(std::abs(rect.bottom - rect.top)), taskbars[i].primary});
            RECT window{};
            if (taskbars[i].primary && !status.taskbarHeight && GetWindowRect(taskbars[i].hWnd, &window)) {
                status.taskbarHeight = double(std::min(window.bottom - window.top, window.right - window.left)) *
                                       96 / WindowDpi(taskbars[i].hWnd);
            }
        }
        // Taskbars that show the bars: on a side, and in the tray instead of beside Start.
        {
            const bool startArea = IsStartAreaPlacement(baseline.settings.placement);
            const bool start = baseline.settings.placement == TaskbarPlacement::RightOfStart;
            std::vector<HWND> shown = FindCurrentProcessTaskbarWnds();
            std::lock_guard<std::mutex> lock(g_placementMutex);
            for (size_t i = 0; i < taskbars.size(); i++) {
                if (std::find(shown.begin(), shown.end(), taskbars[i].hWnd) == shown.end()) continue;
                for (const SideTaskbarRecord& record : g_sideTaskbars) {
                    if (record.hWnd == taskbars[i].hWnd) status.sideTaskbars.push_back({int(i) + 1, record.unsupported});
                }
                for (const PlacementRecord& record : g_placementRecords) {
                    if (startArea && record.hWnd == taskbars[i].hWnd && record.start == start &&
                        record.fallback != PlacementFallback::None) {
                        status.placementFallbacks.push_back({int(i) + 1, record.fallback == PlacementFallback::Conflict,
                                                             record.fallback == PlacementFallback::Side});
                    }
                }
            }
        }
        status.visualTest = g_visualTestMode.load(std::memory_order_acquire);
        status.automaticLanguage = baseline.settings.language.empty() ? g_language.load(std::memory_order_acquire) :
                                                                        &ResolveLanguage({});
        // Where the journal lives (history/journal.h), whether or not anything was recorded.
        status.historyFolder = UserStorageFolder();
        return baseline;
    }

    // Under the edit lock: fresh settings, the requested field set, ApplyOwnedSettings. The
    // taskbar update runs after the lock is released. A language change also rebuilds the usage
    // history window; one alone doesn't wake polling, and one that keeps the shown language
    // (Automatic resolving to the explicit choice) rebuilds no taskbar.
    SettingsSaveResult Apply(SettingsField field, int value, bool& replaceBlocked) override {
        std::unique_lock<std::mutex> configLock(g_configEditMutex);
        Settings settings;
        {
            std::lock_guard<std::mutex> lock(g_settingsMutex);
            settings = g_settings;
        }
        Settings before = settings;
        SettingsAssign(settings, field, value);
        bool language = settings.language != before.language;
        before.language = settings.language;
        bool poll = settings != before;
        const I18nLanguage* shown = g_language.load(std::memory_order_acquire);
        SettingsApplyResult result = ApplyOwnedSettings(std::move(settings), 0, 0, &replaceBlocked);
        bool switched = g_language.load(std::memory_order_acquire) != shown;
        configLock.unlock();
        if (result == SettingsApplyResult::Failed) return SettingsSaveResult::Failed;
        if (result == SettingsApplyResult::Unchanged) return SettingsSaveResult::Unchanged;
        if (poll || switched) FinishSettingsApply(poll);
        if (language) NotifySettingsWindowChanged();
        return SettingsSaveResult::Changed;
    }

    SettingsAccountResult Account(SettingsAccountRef account, SettingsField field, int value,
                                  const std::wstring& text) override {
        AccountTarget target{account.historyId, account.identity};
        // Sign-ins, sign-outs and the dashboard act on the account's current identity.
        if (field == SettingsField::AccountSignIn || field == SettingsField::AccountSignOut ||
            field == SettingsField::AccountDashboard) {
            uint64_t identity = 0;
            {
                std::lock_guard<std::mutex> lock(g_settingsMutex);
                int index = FindAccountTarget(g_settings, target);
                if (index >= 0) identity = AccountIdentityHash(g_settings.accounts[index]);
            }
            SettingsAccountResult result;
            result.missing = !identity;
            if (!identity) return result;
            if (field == SettingsField::AccountSignIn) {
                StartLoginByIdentity(identity);
            } else if (field == SettingsField::AccountDashboard) {
                OpenDashboardForIdentity(identity);
            } else if (!SignOutAccountByIdentity(identity)) {
                result.save = SettingsSaveResult::Failed;
                result.notice = SettingsNoticeCode::SignOutFailed;
            }
            return result;
        }
        AccountCommandResult command =
            field == SettingsField::AccountRemove ? RemoveAccountCommand(target, value != 0) :
            field == SettingsField::AccountMove   ? MoveAccountCommand(target, value) :
            field == SettingsField::AccountHidden ? SetAccountHiddenCommand(target, value != 0) :
            EditAccountCommand(target, [&](AccountConfig& edited) {
                SettingsAccountAssign(edited, field, value, text);
            });
        return Result(command);
    }

    SettingsAccountResult AddAccount(const std::wstring& provider,
                                     const std::vector<std::wstring>& labels) override {
        AccountConfig account;
        account.provider = provider;
        account.showBars[kFableWeeklyBar] = provider == L"antigravity";  // Argon weekly starts on.
        account.showBars[kExtraUsageBar] = provider == L"opencode";     // So does OpenCode Go's monthly.
        return Result(AddAccountCommand(std::move(account), labels));
    }

    bool HasStoredSignIn(uint64_t identity) override { return HasStoredToken(identity); }

    // For the rest of this load's recovery (settings/storage.h): LoadSettings resets it.
    void ConfirmReplace() override {
        Wh_Log(L"Replacing the stored settings that couldn't be read, as confirmed");
        g_settingsReplaceAllowed = true;
    }

    bool Unloading() override { return g_unloading || g_settingsWindowCancelRequested; }

private:
    static SettingsAccountResult Result(const AccountCommandResult& command) {
        SettingsAccountResult result;
        result.save = command.apply == SettingsApplyResult::Failed    ? SettingsSaveResult::Failed :
                      command.apply == SettingsApplyResult::Unchanged ? SettingsSaveResult::Unchanged :
                                                                         SettingsSaveResult::Changed;
        result.missing = command.missing;
        result.replaceBlocked = command.replaceBlocked;
        result.notice = command.message;
        result.reason = command.reason;
        result.kind = command.icon == MB_ICONWARNING     ? SettingsNoticeKind::Warning :
                      command.icon == MB_ICONINFORMATION ? SettingsNoticeKind::Information :
                                                           SettingsNoticeKind::Error;
        result.account = {command.account.historyId, command.account.identity};
        return result;
    }
};
