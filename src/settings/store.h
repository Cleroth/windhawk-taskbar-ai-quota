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
                (accountStatus.plan.find(L"Spark") != std::wstring::npos ||
                 accountStatus.plan.find(L"spark") != std::wstring::npos)) {
                accountStatus.plan.clear();
            }
            accountStatus.signedIn = account.provider != L"antigravity" && HasStoredToken(identity);
            accountStatus.signingIn = signingIn && signingInIdentity == identity;
        }
        // Display N is the Nth taskbar in the order the taskbar code targets (primary first).
        // The preview takes the primary taskbar's height, converted with that taskbar's DPI.
        std::vector<TaskbarDisplayInfo> taskbars = FindCurrentProcessTaskbarDisplays();
        for (size_t i = 0; i < taskbars.size(); i++) {
            const RECT& rect = taskbars[i].rect;
            status.displays.push_back({int(i) + 1, int(std::abs(rect.right - rect.left)),
                                       int(std::abs(rect.bottom - rect.top)), taskbars[i].primary});
            RECT window{};
            if (taskbars[i].primary && !status.taskbarHeight && GetWindowRect(taskbars[i].hWnd, &window)) {
                status.taskbarHeight = double(window.bottom - window.top) * 96 / WindowDpi(taskbars[i].hWnd);
            }
        }
        status.visualTest = g_visualTestMode.load(std::memory_order_acquire);
        // Where the journal lives (history/journal.h), whether or not anything was recorded.
        PWSTR localAppData = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DONT_VERIFY, nullptr,
                                           &localAppData))) {
            status.historyFolder = std::wstring(localAppData) + L"\\TaskbarAIQuota\\" WH_MOD_ID;
        }
        CoTaskMemFree(localAppData);
        wchar_t separator[8] = {};
        if (GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_STHOUSAND, separator,
                            ARRAYSIZE(separator)) > 1) {
            status.groupSeparator = separator[0];
        }
        return baseline;
    }

    // Under the edit lock: fresh settings, the requested field set, ApplyOwnedSettings. The
    // taskbar update runs after the lock is released.
    SettingsSaveResult Apply(SettingsField field, int value) override {
        std::unique_lock<std::mutex> configLock(g_configEditMutex);
        Settings settings;
        {
            std::lock_guard<std::mutex> lock(g_settingsMutex);
            settings = g_settings;
        }
        SettingsAssign(settings, field, value);
        SettingsApplyResult result = ApplyOwnedSettings(std::move(settings));
        configLock.unlock();
        if (result == SettingsApplyResult::Failed) return SettingsSaveResult::Failed;
        if (result == SettingsApplyResult::Unchanged) return SettingsSaveResult::Unchanged;
        FinishSettingsApply();
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
                result.notice = L"The stored sign-in could not be deleted and remains retained for this account.";
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
        return Result(AddAccountCommand(std::move(account), labels));
    }

    bool HasStoredSignIn(uint64_t identity) override { return HasStoredToken(identity); }

    bool Unloading() override { return g_unloading || g_settingsWindowCancelRequested; }

private:
    static SettingsAccountResult Result(const AccountCommandResult& command) {
        SettingsAccountResult result;
        result.save = command.apply == SettingsApplyResult::Failed    ? SettingsSaveResult::Failed :
                      command.apply == SettingsApplyResult::Unchanged ? SettingsSaveResult::Unchanged :
                                                                         SettingsSaveResult::Changed;
        result.missing = command.missing;
        result.notice = command.message;
        result.kind = command.icon == MB_ICONWARNING     ? SettingsNoticeKind::Warning :
                      command.icon == MB_ICONINFORMATION ? SettingsNoticeKind::Information :
                                                           SettingsNoticeKind::Error;
        result.account = {command.account.historyId, command.account.identity};
        return result;
    }
};
