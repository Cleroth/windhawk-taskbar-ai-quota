// Account commands of the settings window: add, edit, remove, reorder and show or hide an
// account, each one transaction on fresh settings under the edit lock, the account resolved
// afresh. Settings thread only. They show nothing: the result says what to tell the user.

// Which account: by history ID, else (none generated) by provider+label identity.
struct AccountTarget {
    uint64_t historyId = 0;
    uint64_t identity = 0;
};

struct AccountCommandResult {
    SettingsApplyResult apply = SettingsApplyResult::Failed;
    bool missing = false;      // The account isn't configured (any more)
    std::wstring message;      // For the user, if not empty
    UINT icon = MB_ICONERROR;  // The message's
    AccountTarget account;     // The account afterwards: added, renamed or edited
};

static bool HasDuplicateAccount(const Settings& settings, uint64_t ignoredIdentity,
                                const AccountConfig& account) {
    for (const auto& existing : settings.accounts) {
        if (ignoredIdentity && AccountIdentityHash(existing) == ignoredIdentity) continue;
        if (existing.provider == account.provider && existing.label == account.label) return true;
    }
    return false;
}

static int FindAccountTarget(const Settings& settings, AccountTarget target) {
    for (size_t i = 0; i < settings.accounts.size(); i++) {
        const AccountConfig& account = settings.accounts[i];
        if (target.historyId ? account.historyId == target.historyId :
                               AccountIdentityHash(account) == target.identity) {
            return int(i);
        }
    }
    return -1;
}

// The published account with an identity, as normalization left it (its history ID). Called
// under the edit lock.
static AccountTarget PublishedAccountTarget(uint64_t identity) {
    std::lock_guard<std::mutex> lk(g_settingsMutex);
    for (const AccountConfig& account : g_settings.accounts) {
        if (AccountIdentityHash(account) == identity) return {account.historyId, identity};
    }
    return {0, identity};
}

// Adds `account` labelled with the first of `labels` its provider doesn't use yet.
static AccountCommandResult AddAccountCommand(AccountConfig account, const std::vector<std::wstring>& labels) {
    AccountCommandResult result;
    std::unique_lock<std::mutex> configLock(g_configEditMutex);
    Settings settings;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        settings = g_settings;
    }
    auto label = std::find_if(labels.begin(), labels.end(), [&](const std::wstring& candidate) {
        AccountConfig labelled = account;
        labelled.label = candidate;
        return !HasDuplicateAccount(settings, 0, labelled);
    });
    if (label == labels.end()) {
        result.message = L"That provider and label are already configured.";
        result.icon = MB_ICONWARNING;
        return result;
    }
    account.label = *label;
    uint64_t identity = AccountIdentityHash(account);
    settings.accounts.push_back(std::move(account));
    result.apply = ApplyOwnedSettings(std::move(settings));
    if (result.apply != SettingsApplyResult::Failed) result.account = PublishedAccountTarget(identity);
    configLock.unlock();
    if (result.apply == SettingsApplyResult::Failed) {
        result.message = L"Could not save the account.";
    } else if (result.apply == SettingsApplyResult::Changed) {
        FinishSettingsApply();
    }
    return result;
}

// Replaces the account with `edit` applied to its fresh settings. Visibility (which a taskbar
// menu can change meanwhile) and the history ID stay the fresh ones; a provider change starts a
// new history. A rename moves the stored sign-in to the new identity, blocking epoch-gated token
// saves across the local copy, settings and delete transaction.
static AccountCommandResult EditAccountCommand(AccountTarget target,
                                               const std::function<void(AccountConfig&)>& edit) {
    AccountCommandResult result;
    std::unique_lock<std::mutex> configLock(g_configEditMutex);
    Settings settings;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        settings = g_settings;
    }
    int index = FindAccountTarget(settings, target);
    if (index < 0) {
        result.missing = true;
        result.message = L"The account was changed from another taskbar window.";
        result.icon = MB_ICONWARNING;
        return result;
    }
    AccountConfig oldAccount = settings.accounts[index];
    AccountConfig newAccount = oldAccount;
    edit(newAccount);
    uint64_t oldIdentity = AccountIdentityHash(oldAccount);
    uint64_t newIdentity = AccountIdentityHash(newAccount);
    bool identityChanged = oldIdentity != newIdentity;
    if (identityChanged && g_loginInProgress.load()) {
        result.message = L"Wait for the current sign-in to finish before changing identity.";
        result.icon = MB_ICONINFORMATION;
        return result;
    }
    if (HasDuplicateAccount(settings, oldIdentity, newAccount)) {
        result.message = L"That provider and label are already configured.";
        result.icon = MB_ICONWARNING;
        return result;
    }
    // The identity is read only while building columns, so its edits take the rebuild path.
    bool onlyBarSelectionChanged = oldAccount.provider == newAccount.provider &&
                                   oldAccount.label == newAccount.label &&
                                   oldAccount.tileIdentity == newAccount.tileIdentity &&
                                   oldAccount.tileText == newAccount.tileText &&
                                   oldAccount.creditsMax == newAccount.creditsMax &&
                                   oldAccount.recordHistory == newAccount.recordHistory &&
                                   oldAccount.historyId == newAccount.historyId &&
                                   oldAccount.showBars != newAccount.showBars;

    bool sameProviderIdentityChange = identityChanged &&
                                      oldAccount.provider == newAccount.provider;
    bool sameProviderRename = sameProviderIdentityChange &&
                              oldAccount.provider != L"antigravity";
    TokenCopyResult tokenCopyResult = TokenCopyResult::SourceMissing;
    bool oldTokenCleared = true;
    ULONGLONG oldAuthEpoch = 0;
    ULONGLONG newAuthEpoch = 0;
    std::unique_lock<std::mutex> authEpochLock(g_authEpochMutex, std::defer_lock);
    if (sameProviderRename) {
        // Block epoch-gated saves only across the local copy/settings/delete transaction.
        authEpochLock.lock();
        oldAuthEpoch = AuthEpochLocked(oldIdentity);
        newAuthEpoch = AuthEpochLocked(newIdentity);
        tokenCopyResult = CopyStoredTokenForRename(oldIdentity, newIdentity);
        if (tokenCopyResult == TokenCopyResult::DestinationOccupied ||
            tokenCopyResult == TokenCopyResult::Failed) {
            authEpochLock.unlock();
            configLock.unlock();
            result.message =
                tokenCopyResult == TokenCopyResult::DestinationOccupied ?
                    L"The renamed account identity already has a retained sign-in. To replace it, "
                    L"add an account with that provider and label, then remove it. The account was "
                    L"not changed." :
                    L"The stored sign-in could not be copied. The account was not changed.";
            return result;
        }
    }

    // Visibility can be toggled from a taskbar menu meanwhile.
    newAccount.hidden = oldAccount.hidden;
    // Renames retain history; switching providers starts a new series during normalization.
    newAccount.historyId = oldAccount.provider == newAccount.provider ? oldAccount.historyId : 0;
    settings.accounts[index] = newAccount;
    result.apply = ApplyOwnedSettings(
        std::move(settings), sameProviderIdentityChange ? oldIdentity : 0,
        sameProviderIdentityChange ? newIdentity : 0);
    if (result.apply == SettingsApplyResult::Failed) {
        bool copiedTokenCleared = tokenCopyResult != TokenCopyResult::Copied ||
                                  ClearStoredToken(newIdentity);
        if (authEpochLock.owns_lock()) authEpochLock.unlock();
        configLock.unlock();
        result.message =
            copiedTokenCleared ?
                L"Could not save the account." :
                L"Could not save the account. The copied sign-in remains under the attempted "
                L"new identity. To delete it, add an account with the attempted provider and "
                L"label, then remove it.";
        return result;
    }
    if (sameProviderRename) {
        oldTokenCleared = ClearStoredToken(oldIdentity);
        BumpAuthEpochLocked(oldIdentity);
        g_authRenameRedirects.push_back(
            {oldIdentity, oldAuthEpoch, newIdentity, newAuthEpoch});
        authEpochLock.unlock();
    }
    result.account = PublishedAccountTarget(newIdentity);
    configLock.unlock();
    if (result.apply == SettingsApplyResult::Changed) {
        // A quota selection change updates in place: bar layouts already hold every bar and
        // only change visibility and spacing; tile layouts regenerate their meter shapes.
        if (onlyBarSelectionChanged) {
            PostUiUpdate();
            if (!g_uiInjected.load(std::memory_order_acquire)) StartRetryInject();
        } else {
            FinishSettingsApply();
        }
    }
    if (!oldTokenCleared) {
        result.message =
            L"The account was renamed, but a duplicate stored sign-in remains under the old "
            L"identity. To delete it, re-add an account with the old provider and label, then "
            L"remove it.";
        result.icon = MB_ICONWARNING;
    }
    NotifySettingsWindowChanged();
    return result;
}

// Removes the account with its stored sign-in (Anthropic, OpenAI; deleted first), then, when
// asked, its recorded usage history, outside every settings lock; if that fails the account
// stays removed and the message says its history was kept. Not during a sign-in.
static AccountCommandResult RemoveAccountCommand(AccountTarget target, bool deleteHistory) {
    AccountCommandResult result;
    std::unique_lock<std::mutex> configLock(g_configEditMutex);
    if (g_loginInProgress.load()) {
        result.message = L"Wait for the current sign-in to finish before removing accounts.";
        result.icon = MB_ICONINFORMATION;
        return result;
    }
    Settings settings;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        settings = g_settings;
    }
    int index = FindAccountTarget(settings, target);
    if (index < 0) {
        result.missing = true;
        return result;
    }
    uint64_t identity = AccountIdentityHash(settings.accounts[index]);
    uint64_t historyId = settings.accounts[index].historyId;
    bool signIn = settings.accounts[index].provider != L"antigravity";
    if (signIn && !ClearStoredTokenAndBumpAuthEpoch(identity)) {
        result.message = L"The stored sign-in could not be deleted, so the account was not removed.";
        return result;
    }
    settings.accounts.erase(settings.accounts.begin() + index);
    result.apply = ApplyOwnedSettings(std::move(settings));
    configLock.unlock();
    if (result.apply == SettingsApplyResult::Failed) {
        if (signIn) {
            RefreshQuotaByIdentity(identity);
            NotifySettingsWindowChanged();
        }
        result.message = signIn ?
            L"The stored sign-in was deleted, but settings could not be saved. The account "
            L"remains configured but signed out." :
            L"Could not remove the account.";
        return result;
    }
    if (result.apply == SettingsApplyResult::Changed) FinishSettingsApply();
    std::wstring error;
    if (deleteHistory && historyId && !DeleteAccountHistory(historyId, &error)) {
        result.message = L"The account was removed, but its usage history could not be deleted and was kept.\n\n" +
                         error;
        result.icon = MB_ICONWARNING;
    }
    return result;
}

// Moves the account to `to` in the account order (clamped).
static AccountCommandResult MoveAccountCommand(AccountTarget target, int to) {
    AccountCommandResult result;
    std::unique_lock<std::mutex> configLock(g_configEditMutex);
    Settings settings;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        settings = g_settings;
    }
    int index = FindAccountTarget(settings, target);
    if (index < 0) {
        result.missing = true;
        return result;
    }
    to = std::clamp(to, 0, int(settings.accounts.size()) - 1);
    if (to == index) {
        result.apply = SettingsApplyResult::Unchanged;
        return result;
    }
    AccountConfig moved = std::move(settings.accounts[index]);
    settings.accounts.erase(settings.accounts.begin() + index);
    settings.accounts.insert(settings.accounts.begin() + to, std::move(moved));
    result.apply = ApplyOwnedSettings(std::move(settings));
    configLock.unlock();
    if (result.apply == SettingsApplyResult::Failed) {
        result.message = L"Could not reorder the account.";
    } else if (result.apply == SettingsApplyResult::Changed) {
        FinishSettingsApply();
    }
    return result;
}

// Shows or hides the account on the taskbar (the desired state), keeping one visible, and
// persists the hidden set as the taskbar menu does.
static AccountCommandResult SetAccountHiddenCommand(AccountTarget target, bool hidden) {
    AccountCommandResult result;
    std::unique_lock<std::mutex> configLock(g_configEditMutex);
    Settings settings;
    {
        std::lock_guard<std::mutex> lk(g_settingsMutex);
        settings = g_settings;
    }
    int index = FindAccountTarget(settings, target);
    if (index < 0) {
        result.missing = true;
        return result;
    }
    int visibleCount = int(std::count_if(settings.accounts.begin(), settings.accounts.end(),
                                         [](const AccountConfig& account) { return !account.hidden; }));
    if (hidden && !settings.accounts[index].hidden && visibleCount <= 1) {
        result.apply = SettingsApplyResult::Unchanged;
        return result;
    }
    settings.accounts[index].hidden = hidden;
    result.apply = ApplyOwnedSettings(settings);
    configLock.unlock();
    if (result.apply == SettingsApplyResult::Failed) {
        result.message = L"Could not update account visibility.";
    } else if (result.apply == SettingsApplyResult::Changed) {
        std::wstring hashes;
        wchar_t buffer[24];
        for (const auto& account : settings.accounts) {
            if (!account.hidden) continue;
            if (!hashes.empty()) hashes += L";";
            swprintf(buffer, ARRAYSIZE(buffer), L"%016llx",
                     (unsigned long long)AccountIdentityHash(account));
            hashes += buffer;
        }
        Wh_SetStringValue(L"hiddenAccounts", hashes.c_str());
        FinishSettingsApply();
    }
    return result;
}
