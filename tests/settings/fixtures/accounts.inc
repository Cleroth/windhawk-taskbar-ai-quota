// Accounts: the real edit controller's account edits and actions against the test store
// (fixtures/edits.inc), and the Accounts page builder.

static AccountConfig TestAccount(PCWSTR provider, PCWSTR label, uint64_t historyId,
                                 std::array<bool, kQuotaBarCount> bars = {true, true, false, false}) {
    AccountConfig account;
    account.provider = provider;
    account.label = label;
    account.historyId = historyId;
    account.showBars = bars;
    return account;
}

static uint64_t Identity(PCWSTR provider, PCWSTR label) {
    return AccountIdentityHash(TestAccount(provider, label, 0));
}

// OAI (OpenAI, 2,500 credits max), CC 1 and CC 2 (Anthropic), Go (Antigravity): history IDs 11-14.
static void AddTestAccounts(Settings& settings) {
    settings.accounts = {TestAccount(L"openai", L"OAI", 11, {true, true, false, true}),
                         TestAccount(L"anthropic", L"CC 1", 12),
                         TestAccount(L"anthropic", L"CC 2", 13, {true, true, true, true}),
                         TestAccount(L"antigravity", L"Go", 14)};
    settings.accounts[0].creditsMax = 2500;
}

static std::wstring AccountOp(SettingsField field, std::wstring label, std::wstring value) {
    return L"account " + std::to_wstring(int(field)) + L" " + label + L"=" + value;
}

// The store's account operations and adds, in order.
static std::vector<std::wstring> AccountOps(const TestStore& store) {
    std::vector<std::wstring> ops;
    for (const std::wstring& entry : store.log) {
        if (entry.starts_with(L"account ") || entry.starts_with(L"add ")) ops.push_back(entry);
    }
    return ops;
}

static const SettingsDraft* DraftOf(const SettingsEdits& edits, uint64_t key, SettingsField field) {
    for (const SettingsDraft& draft : edits.drafts) {
        if (draft.key == key && draft.field == field) return &draft;
    }
    return nullptr;
}

static constexpr SettingsField kLabel = SettingsField::AccountLabel;

// The Accounts page as the mockups show it: OAI signed in (Plus), CC 1 needing a sign-in (Pro),
// CC 2 signed in (Max 20x), Go with its local app running (AI Pro).
static SettingsView AccountsView(std::initializer_list<int> expanded = {}) {
    SettingsView view = TestView(SettingsUiPage::Accounts);
    AddTestAccounts(view.settings);
    const SettingsAccountStatus statuses[] = {
        {L"Plus", {}, true, true, false, false},
        {L"Pro", {AccountErrorCode::NotSignedIn}, false, false, true, false},
        {L"Max 20x", {}, true, true, false, false},
        {L"AI Pro", {}, true, false, false, false},
    };
    for (int i = 0; i < 4; i++) {
        SettingsViewAccount account;
        account.key = view.settings.accounts[size_t(i)].historyId;
        account.slot = uint32_t(i + 1);
        account.status = statuses[i];
        account.expanded = std::find(expanded.begin(), expanded.end(), i) != expanded.end();
        view.accounts.push_back(account);
    }
    return view;
}

static uint32_t AccountId(uint32_t slot, SettingsField field, uint32_t part = 0) {
    return SettingsAccountControlId(slot, field, part);
}

// Nine accounts of every provider with long labels, plans and errors: history IDs (keys) 21-29,
// slots 1-9, cards in `order` (indexes into that list), the listed ones expanded.
static SettingsView NineAccountsView(std::vector<size_t> order, std::initializer_list<size_t> expanded) {
    struct Entry {
        PCWSTR provider, label;
        SettingsAccountStatus status;
    };
    const Entry entries[] = {
        {L"anthropic", L"Claude Max (personal)", {L"Max 20x", {}, true, true, false, false}},
        {L"anthropic", L"Work", {L"Team", {}, true, true, false, false}},
        {L"openai", L"ChatGPT Pro", {L"Pro", {}, true, true, false, false}},
        {L"anthropic", L"Old login",
         {L"Pro", {AccountErrorCode::NotSignedIn}, false, false, true, false}},
        {L"antigravity", L"Gemini desktop", {L"AI Pro", {}, true, false, false, false}},
        {L"openai", L"Codex (team seat)", {L"Plus", {}, true, true, false, false}},
        {L"anthropic", L"Side project",
         {L"Max 5x", {AccountErrorCode::Message, L"HTTP 429: rate limited, retrying in 5 minutes"}, true, true, false,
          false}},
        {L"openai", L"Business", {L"Business", {}, true, true, false, false}},
        {L"antigravity", L"Laptop", {L"", {AccountErrorCode::AntigravityNotRunning}, false, false, false, false}},
    };
    SettingsView view = TestView(SettingsUiPage::Accounts);
    for (size_t i : order) {
        view.settings.accounts.push_back(TestAccount(entries[i].provider, entries[i].label, 21 + i,
                                                     {true, true, i % 3 == 0, i % 2 == 0}));
        view.settings.accounts.back().creditsMax = i == 2 ? 5000 : 0;
        SettingsViewAccount account;
        account.key = 21 + i;
        account.slot = uint32_t(i + 1);
        account.status = entries[i].status;
        account.expanded = std::find(expanded.begin(), expanded.end(), i) != expanded.end();
        view.accounts.push_back(account);
    }
    return view;
}

static void RunAccountEditTests() {
    Run("accounts: keys are history IDs; one without gets a session key that follows its rename", [] {
        TestStore store;
        AddTestAccounts(store.settings);
        store.settings.accounts[1].historyId = 0;
        SettingsEdits edits(store);
        edits.Reload();
        CHECK(edits.keys.size() == 4 && edits.keys[0] == 11 && edits.keys[3] == 14);
        uint64_t session = edits.keys[1];
        CHECK((session >> 63) == 1 && edits.SlotOf(session) == 2 && edits.SlotOf(14) == 4);
        edits.SetText(session, kLabel, L"CC 7");
        CHECK(edits.RequestCommit(session, kLabel) && edits.Process() == 0);
        CHECK(store.settings.accounts[1].label == L"CC 7" && edits.keys[1] == session);
        edits.Reload();
        CHECK(edits.keys[1] == session && edits.SlotOf(session) == 2);
        // A move keeps every key's slot.
        CHECK(edits.AccountAction(14, SettingsField::AccountMove, 0) == SettingsActionResult::Done);
        CHECK(edits.keys[0] == 14 && edits.SlotOf(14) == 4 && edits.keys[2] == session);
        // Its first save generates its history ID: the session key stays, so the close flush still
        // finds and saves its other drafts, and an action after a rename finds the account.
        store.generateHistoryIds = true;
        edits.SetText(session, SettingsField::AccountTileText, L"C7");
        edits.SetText(session, kLabel, L"CC 8");
        CHECK(edits.PrepareClose() == SettingsCloseResult::Saved && edits.drafts.empty());
        CHECK(store.settings.accounts[2].historyId && store.settings.accounts[2].tileText == L"C7" &&
              store.settings.accounts[2].label == L"CC 8");
        CHECK(edits.keys[2] == session && edits.SlotOf(session) == 2);
        edits.SetText(session, kLabel, L"CC 9");
        CHECK(edits.AccountAction(session, SettingsField::AccountSignIn, 0) == SettingsActionResult::Done);
        CHECK(AccountOps(store).back() == AccountOp(SettingsField::AccountSignIn, L"CC 9", L"0"));
        CHECK(edits.notices.empty());
    });

    Run("accounts: empty and duplicate labels get errors and never queue; spaces trim; other providers may share one", [] {
        TestStore store;
        AddTestAccounts(store.settings);
        SettingsEdits edits(store);
        edits.Reload();
        edits.SetText(12, kLabel, L"   ");
        CHECK(!edits.RequestCommit(12, kLabel) && edits.queue.empty());
        CHECK(DraftOf(edits, 12, kLabel)->error == L"Enter a label.");
        edits.SetText(12, kLabel, L"CC 2");
        CHECK(DraftOf(edits, 12, kLabel)->error.empty());  // Typing clears it
        CHECK(!edits.RequestCommit(12, kLabel, true) && edits.queue.empty());
        CHECK(DraftOf(edits, 12, kLabel)->error == L"Another account of this provider uses this label.");
        CHECK(edits.Projection().accounts[1].label == L"CC 1");  // An invalid label doesn't show
        edits.SetText(12, kLabel, L"  OAI ");  // OpenAI's label: fine for Anthropic
        CHECK(edits.Projection().accounts[1].label == L"OAI");
        CHECK(edits.RequestCommit(12, kLabel) && edits.Process() == 0);
        CHECK(AccountOps(store) == std::vector<std::wstring>{AccountOp(kLabel, L"CC 1", L"OAI")});
        CHECK(store.settings.accounts[1].label == L"OAI" && edits.drafts.empty());
        // Esc after a deferred commit skips it.
        edits.SetText(13, kLabel, L"CC 9");
        CHECK(edits.RequestCommit(13, kLabel, true));
        edits.Revert(13, kLabel);
        CHECK(edits.Process() == 0 && AccountOps(store).size() == 1);
    });

    Run("accounts: Sign in after a blurred rename renames first, then signs in under the new identity", [] {
        TestStore store;
        AddTestAccounts(store.settings);
        store.signIns = {Identity(L"anthropic", L"CC 1")};
        SettingsEdits edits(store);
        edits.Reload();
        edits.SetText(12, kLabel, L"CC 3");
        CHECK(edits.RequestCommit(12, kLabel, true));  // Focus moved to the button: queued
        CHECK(edits.AccountAction(12, SettingsField::AccountSignIn, 0) == SettingsActionResult::Done);
        CHECK((AccountOps(store) == std::vector<std::wstring>{AccountOp(kLabel, L"CC 1", L"CC 3"),
                                                               AccountOp(SettingsField::AccountSignIn, L"CC 3", L"0")}));
        CHECK(store.signIns == std::vector<uint64_t>{Identity(L"anthropic", L"CC 3")});
        CHECK(edits.queue.empty() && edits.Process() == 0 && AccountOps(store).size() == 2);
    });

    Run("accounts: an invalid label, queued or not, blocks Sign in; a failed rename blocks it until it saves", [] {
        TestStore store;
        AddTestAccounts(store.settings);
        SettingsEdits edits(store);
        edits.Reload();
        edits.SetText(12, kLabel, L"");  // Typed, never committed
        CHECK(edits.AccountAction(12, SettingsField::AccountSignIn, 0) == SettingsActionResult::Blocked);
        CHECK(AccountOps(store).empty() && DraftOf(edits, 12, kLabel)->error == L"Enter a label.");
        // Valid once, then another account takes the label: blocked again, not sent.
        edits.SetText(12, kLabel, L"CC 4");
        store.settings.accounts[2].label = L"CC 4";
        edits.Reload();
        CHECK(edits.AccountAction(12, SettingsField::AccountSignOut, 0) == SettingsActionResult::Blocked);
        CHECK(AccountOps(store).empty());
        edits.SetText(12, kLabel, L"CC 5");
        store.failNext = 1;
        CHECK(edits.AccountAction(12, SettingsField::AccountSignIn, 0) == SettingsActionResult::Blocked);
        CHECK(AccountOps(store) == std::vector<std::wstring>{AccountOp(kLabel, L"CC 1", L"CC 5")});
        const SettingsDraft* failed = DraftOf(edits, 12, kLabel);
        CHECK(failed && failed->failed && failed->error == L"This change couldn't be saved.");
        CHECK(edits.notices.size() == 1 && edits.notices[0].failure && edits.notices[0].text == L"Injected failure.");
        CHECK(edits.Process() == 0 && AccountOps(store).size() == 1);  // Not retried by itself
        CHECK(edits.AccountAction(12, SettingsField::AccountSignIn, 0) == SettingsActionResult::Done);
        CHECK(AccountOps(store).back() == AccountOp(SettingsField::AccountSignIn, L"CC 5", L"0"));
    });

    Run("accounts: removal drops the account's drafts and requests; other accounts keep theirs", [] {
        TestStore store;
        AddTestAccounts(store.settings);
        store.signIns = {Identity(L"openai", L"OAI"), Identity(L"anthropic", L"CC 2")};
        SettingsEdits edits(store);
        edits.Reload();
        edits.SetText(11, kLabel, L"OAI 2");
        CHECK(edits.RequestCommit(11, kLabel, true));
        edits.SetText(11, SettingsField::AccountCreditsMax, L"12a");  // Invalid: stays through the flush
        edits.SetText(13, SettingsField::AccountTileText, L"C2");
        CHECK(edits.AccountAction(11, SettingsField::AccountRemove, 1) == SettingsActionResult::Done);
        CHECK((AccountOps(store) == std::vector<std::wstring>{AccountOp(kLabel, L"OAI", L"OAI 2"),
                                                               AccountOp(SettingsField::AccountRemove, L"OAI 2", L"1")}));
        CHECK(store.settings.accounts.size() == 3 && !edits.IndexOf(11));
        CHECK(edits.drafts.size() == 1 && edits.drafts[0].key == 13 && edits.queue.empty());
        CHECK(store.signIns == std::vector<uint64_t>{Identity(L"anthropic", L"CC 2")});
    });

    Run("accounts: removal deletes the stored sign-in either way and the history only when asked; a kept history's "
        "notice shows once the account is gone, as no failure", [] {
        TestStore store;
        AddTestAccounts(store.settings);
        store.signIns = {Identity(L"openai", L"OAI"), Identity(L"anthropic", L"CC 1"), Identity(L"anthropic", L"CC 2")};
        SettingsEdits edits(store);
        edits.Reload();
        CHECK(edits.AccountAction(12, SettingsField::AccountRemove, 0) == SettingsActionResult::Done);
        CHECK(store.historyDeleted.empty() && edits.notices.empty());
        CHECK(edits.AccountAction(11, SettingsField::AccountRemove, 1) == SettingsActionResult::Done);
        CHECK(store.historyDeleted == std::vector<uint64_t>{11});
        CHECK(store.signIns == std::vector<uint64_t>{Identity(L"anthropic", L"CC 2")});
        store.historyKept = true;
        CHECK(edits.AccountAction(13, SettingsField::AccountRemove, 1) == SettingsActionResult::Done);
        CHECK(store.signIns.empty() && store.historyDeleted.size() == 1);
        CHECK(!edits.IndexOf(13) && edits.keys == std::vector<uint64_t>{14});
        CHECK(edits.notices.size() == 1 && !edits.notices[0].failure &&
              edits.notices[0].kind == SettingsNoticeKind::Warning);
    });

    Run("accounts: Add takes the first default label without a stored sign-in or a configured account", [] {
        TestStore store;
        AddTestAccounts(store.settings);
        store.settings.accounts.push_back(TestAccount(L"anthropic", L"A3", 15));
        store.signIns = {Identity(L"anthropic", L"A"), Identity(L"anthropic", L"A2"), Identity(L"anthropic", L"A5")};
        SettingsEdits edits(store);
        edits.Reload();
        uint64_t key = edits.AddAccount(L"anthropic");
        CHECK(AccountOps(store) == std::vector<std::wstring>{L"add anthropic A4"});
        std::optional<size_t> index = edits.IndexOf(key);
        CHECK(index && *index == 5 && edits.baseline.settings.accounts[*index].label == L"A4");
        CHECK(store.presenceChecks > 0);
        // A5 has a stored sign-in: the next one skips it.
        CHECK(edits.baseline.settings.accounts[size_t(*edits.IndexOf(edits.AddAccount(L"anthropic")))].label == L"A6");
        CHECK(edits.baseline.settings.accounts[size_t(*edits.IndexOf(edits.AddAccount(L"openai")))].label == L"O");
        CHECK(edits.baseline.settings.accounts[size_t(*edits.IndexOf(edits.AddAccount(L"antigravity")))].label == L"G");
        CHECK(edits.baseline.settings.accounts[size_t(*edits.IndexOf(edits.AddAccount(L"openai")))].label == L"O2");
        CHECK(edits.baseline.settings.accounts[size_t(*edits.IndexOf(edits.AddAccount(L"copilot")))].label == L"C");
        store.signIns.push_back(Identity(L"copilot", L"C2"));
        CHECK(edits.baseline.settings.accounts[size_t(*edits.IndexOf(edits.AddAccount(L"copilot")))].label == L"C3");
        // OpenCode Go: OC, OC2, ...; a stored sign-in of another provider's label doesn't count.
        store.signIns.push_back(Identity(L"copilot", L"OC"));
        CHECK(edits.baseline.settings.accounts[size_t(*edits.IndexOf(edits.AddAccount(L"opencode")))].label == L"OC");
        store.signIns.push_back(Identity(L"opencode", L"OC2"));
        CHECK(edits.baseline.settings.accounts[size_t(*edits.IndexOf(edits.AddAccount(L"opencode")))].label == L"OC3");
        CHECK(AccountOps(store).back() == L"add opencode OC3");
        store.failNext = 1;
        CHECK(edits.AddAccount(L"openai") == 0);
        CHECK(edits.notices.size() == 1 && edits.notices[0].failure);
    });

    Run("accounts: an added account signs in once it is named: behind its saved label, once, never past a label error "
        "or a failed save; Antigravity has no sign-in", [] {
        TestStore store;
        AddTestAccounts(store.settings);
        SettingsEdits edits(store);
        edits.Reload();
        auto signIn = [](PCWSTR label) { return AccountOp(SettingsField::AccountSignIn, label, L"0"); };
        auto lastOps = [&](size_t count) {
            std::vector<std::wstring> ops = AccountOps(store);
            return std::vector<std::wstring>(ops.end() - ptrdiff_t(std::min(count, ops.size())), ops.end());
        };
        // Left unchanged: its default label.
        uint64_t a = edits.AddAccount(L"anthropic");
        CHECK(edits.pendingSignIn == a);
        CHECK(edits.CommitLabel(a, true) && edits.Process() == 0);
        CHECK(lastOps(2) == (std::vector<std::wstring>{L"add anthropic A", signIn(L"A")}));
        // Once: a later rename only renames.
        edits.SetText(a, kLabel, L"Work");
        CHECK(edits.CommitLabel(a, false) && edits.Process() == 0);
        CHECK(lastOps(1) == std::vector<std::wstring>{AccountOp(kLabel, L"A", L"Work")});
        // Named: the label saves first.
        uint64_t o = edits.AddAccount(L"openai");
        edits.SetText(o, kLabel, L"Personal");
        CHECK(edits.CommitLabel(o, false) && edits.Process() == 0);
        CHECK(lastOps(2) == (std::vector<std::wstring>{AccountOp(kLabel, L"O", L"Personal"), signIn(L"Personal")}));
        // A label error queues nothing; the fixed label saves and signs in.
        uint64_t c = edits.AddAccount(L"copilot");
        edits.SetText(c, kLabel, L"");
        CHECK(!edits.CommitLabel(c, true) && edits.queue.empty() && edits.pendingSignIn == c);
        edits.SetText(c, kLabel, L"GH");
        CHECK(edits.CommitLabel(c, false) && edits.Process() == 0);
        CHECK(lastOps(2) == (std::vector<std::wstring>{AccountOp(kLabel, L"C", L"GH"), signIn(L"GH")}));
        // A failed save keeps it waiting: focus loss doesn't retry the label, Enter does.
        uint64_t oc = edits.AddAccount(L"opencode");
        edits.SetText(oc, kLabel, L"Go plan");
        store.failNext = 1;
        CHECK(edits.CommitLabel(oc, false) && edits.Process() == 1);
        CHECK(lastOps(1) == std::vector<std::wstring>{AccountOp(kLabel, L"OC", L"Go plan")});
        CHECK(edits.pendingSignIn == oc && edits.queue.empty());
        edits.notices.clear();
        CHECK(!edits.CommitLabel(oc, true) && edits.queue.empty());
        CHECK(edits.CommitLabel(oc, false) && edits.Process() == 0);
        CHECK(lastOps(2) == (std::vector<std::wstring>{AccountOp(kLabel, L"OC", L"Go plan"), signIn(L"Go plan")}));
        uint64_t g = edits.AddAccount(L"antigravity");
        CHECK(!edits.pendingSignIn && !edits.CommitLabel(g, true) && edits.queue.empty());
        CHECK(edits.notices.empty());
    });

    Run("accounts: a sign-in queued by naming survives a page change and the card's other actions; Sign in replaces "
        "it; removal drops a waiting one", [] {
        TestStore store;
        AddTestAccounts(store.settings);
        SettingsEdits edits(store);
        edits.Reload();
        auto lastOps = [&](size_t count) {
            std::vector<std::wstring> ops = AccountOps(store);
            return std::vector<std::wstring>(ops.end() - ptrdiff_t(std::min(count, ops.size())), ops.end());
        };
        // Focus moved to another page: the label saves, then the sign-in runs.
        uint64_t a = edits.AddAccount(L"anthropic");
        edits.SetText(a, kLabel, L"Work");
        CHECK(edits.CommitLabel(a, true) && edits.Navigate() == 0 && edits.queue.empty());
        CHECK(lastOps(2) == (std::vector<std::wstring>{AccountOp(kLabel, L"A", L"Work"),
                                                       AccountOp(SettingsField::AccountSignIn, L"Work", L"0")}));
        // Focus moved to the card's switch: its action saves the label; the sign-in stays queued.
        uint64_t o = edits.AddAccount(L"openai");
        edits.SetText(o, kLabel, L"Home");
        CHECK(edits.CommitLabel(o, true));
        CHECK(edits.AccountAction(o, SettingsField::AccountHidden, 1) == SettingsActionResult::Done);
        CHECK(edits.Process() == 0);
        CHECK(lastOps(3) == (std::vector<std::wstring>{AccountOp(kLabel, L"O", L"Home"),
                                                       AccountOp(SettingsField::AccountHidden, L"Home", L"1"),
                                                       AccountOp(SettingsField::AccountSignIn, L"Home", L"0")}));
        // Its Sign in button, once the queued one waits: one sign-in.
        uint64_t c = edits.AddAccount(L"copilot");
        CHECK(edits.CommitLabel(c, true));
        CHECK(edits.AccountAction(c, SettingsField::AccountSignIn, 0) == SettingsActionResult::Done);
        CHECK(edits.queue.empty() && !edits.pendingSignIn && !edits.CommitLabel(c, true) && edits.Process() == 0);
        CHECK(lastOps(2) == (std::vector<std::wstring>{L"add copilot C", AccountOp(SettingsField::AccountSignIn, L"C", L"0")}));
        // Removed before its naming.
        uint64_t oc = edits.AddAccount(L"opencode");
        CHECK(edits.AccountAction(oc, SettingsField::AccountRemove, 0) == SettingsActionResult::Done);
        CHECK(!edits.pendingSignIn && edits.notices.empty());
    });

    Run("accounts: actions take desired values on the fresh account; the last visible stays; moves go to an index", [] {
        TestStore store;
        AddTestAccounts(store.settings);
        SettingsEdits edits(store);
        edits.Reload();
        CHECK(edits.AccountAction(11, SettingsField::AccountHidden, 1) == SettingsActionResult::Done);
        CHECK(edits.AccountAction(11, SettingsField::AccountHidden, 1) == SettingsActionResult::Done);
        CHECK(store.settings.accounts[0].hidden);  // Desired, not toggled
        edits.AccountAction(12, SettingsField::AccountHidden, 1);
        edits.AccountAction(13, SettingsField::AccountHidden, 1);
        edits.AccountAction(14, SettingsField::AccountHidden, 1);
        CHECK(!store.settings.accounts[3].hidden);
        CHECK(edits.AccountAction(14, SettingsField::AccountMove, 1) == SettingsActionResult::Done);
        CHECK(store.settings.accounts[1].label == L"Go" && edits.keys[1] == 14 && edits.keys[3] == 13);
        // A taskbar menu shows OAI while CC 1's label is being typed: the draft stays, and its
        // save keeps the fresh visibility.
        edits.SetText(12, kLabel, L"CC 8");
        store.settings.accounts[0].hidden = false;
        store.settings.accounts[2].hidden = false;
        edits.Reload();
        CHECK(DraftOf(edits, 12, kLabel) && edits.Projection().accounts[2].label == L"CC 8");
        CHECK(!edits.Projection().accounts[2].hidden);
        CHECK(edits.RequestCommit(12, kLabel) && edits.Process() == 0);
        CHECK(store.settings.accounts[2].label == L"CC 8" && !store.settings.accounts[2].hidden);
    });

    Run("accounts: a vanished account's drafts go with a notice; discrete edits and credit steps save", [] {
        TestStore store;
        AddTestAccounts(store.settings);
        SettingsEdits edits(store);
        edits.Reload();
        edits.SetText(12, kLabel, L"CC 6");
        CHECK(edits.RequestCommit(12, kLabel));
        store.settings.accounts.erase(store.settings.accounts.begin() + 1);
        CHECK(edits.Process() == 1 && edits.drafts.empty());
        CHECK(edits.notices.size() == 1 && edits.notices[0].kind == SettingsNoticeKind::Warning);
        edits.notices.clear();
        // An invalid label of an account that vanishes goes at the next reload; it can't block
        // closing.
        edits.SetText(13, kLabel, L"");
        AccountConfig gone = store.settings.accounts[1];
        store.settings.accounts.erase(store.settings.accounts.begin() + 1);
        edits.Reload();
        CHECK(edits.drafts.empty() && edits.notices.size() == 1 && !edits.notices[0].failure);
        CHECK(edits.PrepareClose() == SettingsCloseResult::Saved);
        store.settings.accounts.insert(store.settings.accounts.begin() + 1, gone);
        edits.Reload();
        edits.notices.clear();
        CHECK(edits.Commit(11, SettingsField::AccountBars, 0b0011) == 0);
        CHECK(!store.settings.accounts[0].showBars[kExtraUsageBar]);
        CHECK(edits.Commit(13, SettingsField::AccountTileIdentity, int(TileIdentity::Text)) == 0);
        CHECK(edits.Commit(13, SettingsField::AccountRecordHistory, 0) == 0);
        CHECK(store.settings.accounts[1].tileIdentity == TileIdentity::Text && !store.settings.accounts[1].recordHistory);
        // The identity saves on the account alone: the others keep the default logo and badge.
        CHECK(store.settings.accounts[0].tileIdentity == TileIdentity::LogoBadge &&
              store.settings.accounts[2].tileIdentity == TileIdentity::LogoBadge);
        CHECK(SettingsAccountValue(store.settings.accounts[1], SettingsField::AccountTileIdentity) ==
              int(TileIdentity::Text));
        CHECK(edits.drafts.empty());
        uint64_t stepped = edits.Step(11, SettingsField::AccountCreditsMax, 1);
        CHECK(DraftOf(edits, 11, SettingsField::AccountCreditsMax)->raw == L"2,501");
        CHECK(edits.CommitStep(11, SettingsField::AccountCreditsMax, stepped) && edits.Process() == 0);
        CHECK(store.settings.accounts[0].creditsMax == 2501);
        edits.SetText(11, SettingsField::AccountCreditsMax, L"1,000,000,000");
        CHECK(edits.Projection().accounts[0].creditsMax == 999999999);
        edits.SetText(13, SettingsField::AccountTileText, L"C2");
        CHECK(edits.RequestCommit(13, SettingsField::AccountTileText) && edits.Process() == 0);
        CHECK(store.settings.accounts[1].tileText == L"C2");
    });

    Run("accounts: unload during the drafts' saves stops the action and Add", [] {
        TestStore store;
        AddTestAccounts(store.settings);
        SettingsEdits edits(store);
        edits.Reload();
        edits.SetText(12, kLabel, L"CC 3");
        store.unloadAfter = 1;
        CHECK(edits.AccountAction(12, SettingsField::AccountHidden, 1) == SettingsActionResult::Blocked);
        CHECK(AccountOps(store) == std::vector<std::wstring>{AccountOp(kLabel, L"CC 1", L"CC 3")});
        CHECK(!store.settings.accounts[1].hidden);
        CHECK(edits.AccountAction(13, SettingsField::AccountRemove, 1) == SettingsActionResult::Blocked);
        CHECK(edits.AddAccount(L"openai") == 0 && AccountOps(store).size() == 1 && store.settings.accounts.size() == 4);
    });

    Run("accounts: navigation reverts an invalid label; close preparation blocks on one", [] {
        TestStore store;
        AddTestAccounts(store.settings);
        SettingsEdits edits(store);
        edits.Reload();
        edits.SetText(12, kLabel, L"");
        edits.SetText(13, kLabel, L"CC 3");
        CHECK(edits.Navigate() == 0);
        CHECK(AccountOps(store) == std::vector<std::wstring>{AccountOp(kLabel, L"CC 2", L"CC 3")});
        CHECK(edits.drafts.empty() && store.settings.accounts[1].label == L"CC 1");
        edits.SetText(12, kLabel, L"CC 3");
        CHECK(edits.PrepareClose() == SettingsCloseResult::Blocked);
        CHECK(DraftOf(edits, 12, kLabel)->error == L"Another account of this provider uses this label.");
    });

    Run("reorder target: past every other card whose center is above the pointer", [] {
        std::vector<float> centers = {34, 106, 178};
        CHECK(SettingsReorderTarget(centers, 10) == 0);
        CHECK(SettingsReorderTarget(centers, 40) == 1);
        CHECK(SettingsReorderTarget(centers, 177) == 2);
        CHECK(SettingsReorderTarget(centers, 500) == 3);
        CHECK(SettingsReorderTarget({}, 50) == 0);
    });

    Run("accounts page: cards with logo, label, status and quota keys; Sign in where needed; ids by slot", [] {
        UiGraphics& g = TestGraphics();
        SettingsView view = AccountsView();
        UiFrame frame;
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        // Pinned header: title, Add account (right-aligned in the column), the note.
        const UiItem* add = UiFindItem(frame, SettingsControlId(SettingsField::AccountAdd));
        CHECK(add && add->kind == UiItemKind::Button && add->button == UiButtonStyle::Accent && add->options.size() == 5);
        CHECK_NEAR(add->rect.right, 260 + 20 + 680);
        CHECK_NEAR(add->rect.top, 14);
        CHECK(add->options[2].detail == L"Uses the signed-in Antigravity app or CLI" && add->action.value == -1);
        // GitHub Copilot, by its value the controller's fourth provider; OpenCode Go last, its fifth.
        CHECK(add->options[3].text == L"GitHub Copilot" && add->options[3].value == 3 &&
              add->options[3].detail == L"Sign in with a code on github.com" &&
              add->options[3].parts.size() == 1 && add->options[3].parts[0].path == kCopilotLogoPath);
        CHECK(add->options[4].text == L"OpenCode Go" && add->options[4].value == 4 &&
              add->options[4].detail == L"Sign in with an API key from the OpenCode console" &&
              add->options[4].parts.size() == 1 && add->options[4].parts[0].path == kOpenCodeLogoPath &&
              add->options[4].parts[0].ink == UiInk::Primary && !add->options[4].parts[0].fill);
        CHECK(std::size(kSettingsProviders) == 5 && kSettingsProviders[3] == std::wstring_view(L"copilot") &&
              kSettingsProviders[4] == std::wstring_view(L"opencode"));
        // Each option's value names the provider whose mark it shows.
        for (const UiOption& option : add->options) {
            CHECK(option.value >= 0 && size_t(option.value) < std::size(kSettingsProviders) &&
                  option.parts.at(0).path == ProviderLogoPath(kSettingsProviders[option.value]));
        }
        CHECK_NEAR(frame.viewport.top, 69);
        // Cards: 68 DIP collapsed, 4 DIP apart from 14 DIP down.
        for (uint32_t slot = 1; slot <= 4; slot++) {
            const UiItem* panel = UiFindItem(frame, AccountId(slot, SettingsField::AccountExpand, 1));
            const UiItem* header = UiFindItem(frame, AccountId(slot, SettingsField::AccountExpand));
            CHECK(panel && header && header->button == UiButtonStyle::Subtle && header->action.value == 1);
            CHECK_NEAR(panel->rect.top, 14 + 72.0f * float(slot - 1));
            CHECK_NEAR(panel->rect.bottom - panel->rect.top, 68);
            CHECK(header->action.key == view.accounts[slot - 1].key);
            const UiItem* handle = UiFindItem(frame, AccountId(slot, SettingsField::AccountMove));
            CHECK(handle && handle->kind == UiItemKind::Handle && handle->owner == header->id);
            const UiItem* visible = UiFindItem(frame, AccountId(slot, SettingsField::AccountHidden));
            CHECK(visible && visible->kind == UiItemKind::Toggle && visible->selected && visible->action.value == 1);
        }
        CHECK(HasText(frame, L"Signed in") && HasText(frame, L"Sign-in needed") && HasText(frame, L"Local app running"));
        CHECK(HasText(frame, L"Anthropic \u00B7 Max 20x \u00B7 "));
        // Only CC 1 offers Sign in in its header.
        CHECK(UiFindItem(frame, AccountId(2, SettingsField::AccountSignIn)) &&
              !UiFindItem(frame, AccountId(1, SettingsField::AccountSignIn)));
        std::vector<std::wstring> keys;
        for (const UiItem& item : frame.content) {
            if (item.kind == UiItemKind::Chip && item.rect.top < 14 + 68) keys.push_back(item.text);
        }
        CHECK((keys == std::vector<std::wstring>{L"5h", L"7d", L"Cr"}));
        // Each card's logo (OAI, CC 1, CC 2, Go) in the primary ink; with brand colors chosen,
        // Claude's fill orange and Gemini's purple to blue, OpenAI's stays.
        auto logos = [](const UiFrame& f) {
            std::vector<const UiShapePart*> parts;
            for (const UiItem& item : f.content) {
                if (item.kind != UiItemKind::Shape || item.parts.size() != 1) continue;
                PCWSTR path = item.parts[0].path;
                if (path == kOpenAiLogoPath || path == kClaudeLogoPath || path == kGeminiLogoPath) parts.push_back(&item.parts[0]);
            }
            return parts;
        };
        std::vector<const UiShapePart*> plain = logos(frame);
        CHECK(plain.size() == 4 && plain[0]->path == kOpenAiLogoPath && plain[1]->path == kClaudeLogoPath &&
              plain[3]->path == kGeminiLogoPath);
        for (const UiShapePart* logo : plain) CHECK(logo->ink == UiInk::Primary && !logo->fill);
        SettingsView branded = view;
        branded.settings.logoBrands = {true, true};
        UiFrame brandedFrame;
        SettingsUiBuildFrame(g, branded, {1000, 820}, brandedFrame);
        std::vector<const UiShapePart*> brand = logos(brandedFrame);
        CHECK(brand.size() == 4 && !brand[0]->fill && brand[1]->fill && brand[2]->fill && brand[3]->fill);
        CHECK(UiColorKey(brand[1]->fill->from) == 0xFFD97757 && UiColorKey(brand[1]->fill->to) == 0xFFD97757 &&
              UiColorKey(brand[3]->fill->from) == 0xFF9168C0 && UiColorKey(brand[3]->fill->to) == 0xFF1BA1E3);
        for (const UiShapePart* logo : brand) CHECK(logo->ink == UiInk::Primary);
        // Chips that only show pass the pointer to the header.
        const UiItem* chip = &*std::find_if(frame.content.begin(), frame.content.end(),
                                            [](const UiItem& item) { return item.kind == UiItemKind::Chip; });
        D2D1_POINT_2F onChip{(chip->rect.left + chip->rect.right) / 2, (chip->rect.top + chip->rect.bottom) / 2};
        CHECK(UiHitTest(g, frame.content, onChip).id == AccountId(1, SettingsField::AccountExpand));
        // Focus order of a card: header, (Sign in), visibility.
        std::vector<UiFocusStop> stops = UiFocusStops(frame);
        CHECK(stops.size() == 6 + 1 + 4 * 2 + 1);
        auto stop = [&](uint32_t id) {
            return std::find_if(stops.begin(), stops.end(), [&](const UiFocusStop& s) { return s.ref.id == id; }) -
                   stops.begin();
        };
        CHECK(stop(AccountId(2, SettingsField::AccountExpand)) + 1 == stop(AccountId(2, SettingsField::AccountSignIn)));
        CHECK(stop(AccountId(2, SettingsField::AccountSignIn)) + 1 == stop(AccountId(2, SettingsField::AccountHidden)));
        // The last visible account's switch can't turn off.
        for (int i = 0; i < 3; i++) view.settings.accounts[size_t(i)].hidden = true;
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(!UiFindItem(frame, AccountId(4, SettingsField::AccountHidden))->enabled);
        CHECK(UiFindItem(frame, AccountId(1, SettingsField::AccountHidden))->enabled);
        CHECK(HasText(frame, L"Hidden, not polled"));
    });

    Run("accounts page: an expanded card's rows per provider; Short text needs Label; chips keep one", [] {
        UiGraphics& g = TestGraphics();
        SettingsView view = AccountsView({0, 3});
        UiFrame frame;
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        const UiItem* label = UiFindItem(frame, AccountId(1, SettingsField::AccountLabel));
        CHECK(label && label->kind == UiItemKind::TextBox && label->text == L"OAI" && label->maximum == 64);
        CHECK_NEAR(label->rect.right - label->rect.left, 200);
        const UiItem* credits = UiFindItem(frame, AccountId(1, SettingsField::AccountCreditsMax));
        CHECK(credits && credits->kind == UiItemKind::NumberBox && credits->text == L"2,500");
        // The identity rows: Identity (logo + number badge by default; Label), then Short text for
        // tiles, disabled until Identity is Label. No Logo on bars switch.
        const UiItem* identity = UiFindItem(frame, AccountId(1, SettingsField::AccountTileIdentity));
        CHECK(identity && identity->kind == UiItemKind::Dropdown && identity->text == L"Logo + number badge");
        CHECK(identity->options.size() == 3 && identity->options[0].text == L"Logo + number badge" &&
              identity->options[1].text == L"Logo only" && identity->options[2].text == L"Label" &&
              identity->options[2].value == int(TileIdentity::Text));
        const UiItem* shortText = UiFindItem(frame, AccountId(1, SettingsField::AccountTileText));
        CHECK(shortText && !shortText->enabled && shortText->maximum == 4 && UiRunsText(shortText->runs) == L"OAI");
        CHECK(shortText->rect.top > identity->rect.bottom);
        CHECK(HasText(frame, L"Marks this account beside its bars and on tiles.") &&
              HasText(frame, L"Label shows the label on bars and its short text on tiles."));
        CHECK(HasText(frame, L"The Label identity on tiles, up to 4 characters.") &&
              HasText(frame, L"Used on tiles when Identity is Label."));
        CHECK(!HasText(frame, L"Logo on bars") && !HasText(frame, L"Tile identity") && !HasText(frame, L"Label text"));
        CHECK(HasText(frame, L"Shown in details, and next to bars when Identity is Label."));
        // Its order: Identity, then (Short text disabled) Record history; no switch between.
        std::vector<UiFocusStop> stops = UiFocusStops(frame);
        auto stop = [&](uint32_t id) {
            return std::find_if(stops.begin(), stops.end(), [&](const UiFocusStop& s) { return s.ref.id == id; }) -
                   stops.begin();
        };
        CHECK(stop(AccountId(1, SettingsField::AccountTileIdentity)) + 1 ==
              stop(AccountId(1, SettingsField::AccountRecordHistory)));
        CHECK(HasText(frame, L"\u25CF Recording"));
        // OpenAI chips: 5-hour, Weekly, Credits, each the desired mask with itself flipped.
        const UiItem* weekly = UiFindItem(frame, AccountId(1, SettingsField::AccountBars, kWeeklyBar));
        CHECK(weekly && weekly->selected && weekly->action.value == 0b1001 && weekly->text == L"Weekly");
        CHECK(!UiFindItem(frame, AccountId(1, SettingsField::AccountBars, kFableWeeklyBar)));
        // Footer: dashboard, Sign in again, Sign out, Remove account at the right.
        CHECK(UiFindItem(frame, AccountId(1, SettingsField::AccountDashboard, 1))->icon == L"\uE8A7");
        CHECK(UiFindItem(frame, AccountId(1, SettingsField::AccountSignIn, 1))->text == L"Sign in again");
        CHECK(UiFindItem(frame, AccountId(1, SettingsField::AccountSignOut, 1)));
        const UiItem* remove = UiFindItem(frame, AccountId(1, SettingsField::AccountRemove, 1));
        CHECK(remove && remove->button == UiButtonStyle::Danger);
        CHECK_NEAR(remove->rect.right, 680 + 20 - 19);
        // Antigravity: no dashboard or sign-in; its first quota is the short window.
        CHECK(!UiFindItem(frame, AccountId(4, SettingsField::AccountDashboard, 1)) &&
              UiFindItem(frame, AccountId(4, SettingsField::AccountRemove, 1)));
        CHECK(UiFindItem(frame, AccountId(4, SettingsField::AccountBars, kFiveHourBar))->text == L"Short window");
        // Then Weekly and Argon weekly (Gemini Argon, in the third slot); no extra usage.
        const UiItem* argon = UiFindItem(frame, AccountId(4, SettingsField::AccountBars, kFableWeeklyBar));
        CHECK(argon && argon->text == L"Argon weekly" && !argon->selected && argon->enabled &&
              argon->action.value == 0b0111);
        CHECK(!UiFindItem(frame, AccountId(4, SettingsField::AccountBars, kExtraUsageBar)));
        // Selected, its header keys it "Ar".
        view.settings.accounts[3].showBars = {true, false, true, false};
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(UiFindItem(frame, AccountId(4, SettingsField::AccountBars, kFableWeeklyBar))->selected);
        CHECK(std::any_of(frame.content.begin(), frame.content.end(), [](const UiItem& item) {
            return item.kind == UiItemKind::Chip && item.text == L"Ar";
        }));
        view.settings.accounts[3].showBars = {true, true, false, false};
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(UiFindItem(frame, AccountId(1, SettingsField::AccountExpand))->action.value == 0);

        // An expired sign-in (stored, rejected): the header's Sign in only; every id unique.
        view.accounts[0].status.needsSignIn = true;
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(UiFindItem(frame, AccountId(1, SettingsField::AccountSignIn)) &&
              !UiFindItem(frame, AccountId(1, SettingsField::AccountSignIn, 1)) &&
              UiFindItem(frame, AccountId(1, SettingsField::AccountSignOut, 1)));
        std::vector<uint32_t> ids;
        for (const std::vector<UiItem>* list : {&frame.main, &frame.content}) {
            for (const UiItem& item : *list) ids.push_back(item.id);
        }
        std::sort(ids.begin(), ids.end());
        CHECK(std::adjacent_find(ids.begin(), ids.end()) == ids.end());
        view.accounts[0].status.needsSignIn = false;

        // Label enables Short text; the last chip can't clear; no credits max disables Credits.
        view.settings.accounts[0].tileIdentity = TileIdentity::Text;
        view.settings.accounts[0].showBars = {false, true, false, false};
        view.settings.accounts[0].creditsMax = 0;
        view.drafts.push_back({11, kLabel, 1, true, L"", 0, false, false, L"Enter a label."});
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(UiFindItem(frame, AccountId(1, SettingsField::AccountTileText))->enabled);
        CHECK(UiFindItem(frame, AccountId(1, SettingsField::AccountTileIdentity))->text == L"Label");
        CHECK(UiFindItem(frame, AccountId(1, SettingsField::AccountBars, kWeeklyBar))->action.value == 0b0010);
        CHECK(!UiFindItem(frame, AccountId(1, SettingsField::AccountBars, kExtraUsageBar))->enabled);
        CHECK(HasText(frame, L"Set Credits max to show the Credits bar."));
        CHECK(UiFindItem(frame, AccountId(1, SettingsField::AccountLabel))->text.empty() && HasText(frame, L"Enter a label."));
        // Sparklines: the OpenAI card says where its credits show.
        constexpr std::wstring_view kSparklineCredits = L"Usage graphs show Credits as a number on the 7d chart.";
        CHECK(!HasText(frame, kSparklineCredits));
        view.settings.barLayout = BarLayout::Sparkline;
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(HasText(frame, L"At least one stays selected. Usage graphs show Credits as a number on the 7d chart."));
    });

    Run("accounts page: a GitHub Copilot card: its mark, the AI credits quota keyed Mo, sign-in and dashboard", [] {
        UiGraphics& g = TestGraphics();
        SettingsView view = TestView(SettingsUiPage::Accounts);
        view.settings.accounts = {TestAccount(L"copilot", L"C", 31, {false, false, false, true})};
        SettingsViewAccount card;
        card.key = 31;
        card.slot = 1;
        card.status = {L"Free", {}, true, true, false, false};
        card.expanded = true;
        view.accounts.push_back(card);
        UiFrame frame;
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        // Header: the Copilot mark, the provider with the plan, one key chip.
        CHECK(std::any_of(frame.content.begin(), frame.content.end(), [](const UiItem& item) {
            return item.kind == UiItemKind::Shape && item.parts.size() == 1 && item.parts[0].path == kCopilotLogoPath;
        }));
        CHECK(HasText(frame, L"GitHub Copilot \u00B7 Free \u00B7 Signed in"));
        std::vector<std::wstring> keys;
        for (const UiItem& item : frame.content) {
            if (item.kind == UiItemKind::Chip && item.rect.top < 14 + 68) keys.push_back(item.text);
        }
        CHECK((keys == std::vector<std::wstring>{L"Mo"}));
        // Its one quota stays selected; no credits max; the browser sign-in's actions.
        const UiItem* credits = UiFindItem(frame, AccountId(1, SettingsField::AccountBars, kExtraUsageBar));
        CHECK(credits && credits->text == L"AI credits" && credits->selected && credits->enabled &&
              credits->action.value == 0b1000);
        CHECK(!UiFindItem(frame, AccountId(1, SettingsField::AccountBars, kFiveHourBar)) &&
              !UiFindItem(frame, AccountId(1, SettingsField::AccountBars, kWeeklyBar)) &&
              !UiFindItem(frame, AccountId(1, SettingsField::AccountCreditsMax)));
        CHECK(UiFindItem(frame, AccountId(1, SettingsField::AccountDashboard, 1)) &&
              UiFindItem(frame, AccountId(1, SettingsField::AccountSignIn, 1))->text == L"Sign in again" &&
              UiFindItem(frame, AccountId(1, SettingsField::AccountSignOut, 1)));
        // Without a sign-in: Sign in in its header.
        view.accounts[0].status = {L"", {AccountErrorCode::NotSignedIn}, false, false, true,
                                   false};
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(UiFindItem(frame, AccountId(1, SettingsField::AccountSignIn)) && HasText(frame, L"Sign-in needed"));
    });

    Run("accounts page: an OpenCode Go card: its mark without brand colors, its 5-hour, weekly and monthly quotas "
        "(the monthly keyed Mo), sign-in and dashboard", [] {
        UiGraphics& g = TestGraphics();
        SettingsView view = TestView(SettingsUiPage::Accounts);
        view.settings.accounts = {TestAccount(L"opencode", L"OC", 32, {true, true, false, true})};
        view.settings.logoBrands = {true, true};
        SettingsViewAccount card;
        card.key = 32;
        card.slot = 1;
        card.status = {L"", {}, true, true, false, false};
        card.expanded = true;
        view.accounts.push_back(card);
        UiFrame frame;
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        // Header: the OpenCode mark in the primary ink, the provider without a plan (it reports
        // none), a key chip per selected quota.
        CHECK(std::any_of(frame.content.begin(), frame.content.end(), [](const UiItem& item) {
            return item.kind == UiItemKind::Shape && item.parts.size() == 1 && item.parts[0].path == kOpenCodeLogoPath &&
                   item.parts[0].ink == UiInk::Primary && !item.parts[0].fill;
        }));
        CHECK(HasText(frame, L"OpenCode Go \u00B7 Signed in"));
        std::vector<std::wstring> keys;
        for (const UiItem& item : frame.content) {
            if (item.kind == UiItemKind::Chip && item.rect.top < 14 + 68) keys.push_back(item.text);
        }
        CHECK((keys == std::vector<std::wstring>{L"5h", L"7d", L"Mo"}));
        // Quotas on taskbar: 5-hour, Weekly and Monthly, each the mask with itself flipped; no third
        // slot, no credits max.
        const UiItem* five = UiFindItem(frame, AccountId(1, SettingsField::AccountBars, kFiveHourBar));
        const UiItem* weekly = UiFindItem(frame, AccountId(1, SettingsField::AccountBars, kWeeklyBar));
        const UiItem* monthly = UiFindItem(frame, AccountId(1, SettingsField::AccountBars, kExtraUsageBar));
        CHECK(five && five->text == L"5-hour" && five->selected && five->enabled && five->action.value == 0b1010);
        CHECK(weekly && weekly->text == L"Weekly" && weekly->selected && weekly->enabled && weekly->action.value == 0b1001);
        CHECK(monthly && monthly->text == L"Monthly" && monthly->selected && monthly->enabled &&
              monthly->action.value == 0b0011);
        CHECK(five->rect.right < weekly->rect.left && weekly->rect.right < monthly->rect.left);
        CHECK(!UiFindItem(frame, AccountId(1, SettingsField::AccountBars, kFableWeeklyBar)) &&
              !UiFindItem(frame, AccountId(1, SettingsField::AccountCreditsMax)));
        CHECK(!HasText(frame, L"Set Credits max"));
        // Footer: dashboard, Sign in again, Sign out; no Sign in in the header.
        CHECK(UiFindItem(frame, AccountId(1, SettingsField::AccountDashboard, 1)) &&
              UiFindItem(frame, AccountId(1, SettingsField::AccountSignIn, 1))->text == L"Sign in again" &&
              UiFindItem(frame, AccountId(1, SettingsField::AccountSignOut, 1)) &&
              !UiFindItem(frame, AccountId(1, SettingsField::AccountSignIn)));
        // Monthly alone: its chip stays selected; the header keys only it.
        view.settings.accounts[0].showBars = {false, false, false, true};
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(UiFindItem(frame, AccountId(1, SettingsField::AccountBars, kExtraUsageBar))->action.value == 0b1000);
        CHECK(UiFindItem(frame, AccountId(1, SettingsField::AccountBars, kWeeklyBar))->action.value == 0b1010);
        keys.clear();
        for (const UiItem& item : frame.content) {
            if (item.kind == UiItemKind::Chip && item.rect.top < 14 + 68) keys.push_back(item.text);
        }
        CHECK((keys == std::vector<std::wstring>{L"Mo"}));
        // Without a sign-in: Sign in in its header; the footer keeps only the dashboard.
        view.accounts[0].status = {L"", {AccountErrorCode::NotSignedIn}, false, false, true,
                                   false};
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(UiFindItem(frame, AccountId(1, SettingsField::AccountSignIn)) && HasText(frame, L"Sign-in needed"));
        CHECK(UiFindItem(frame, AccountId(1, SettingsField::AccountDashboard, 1)) &&
              !UiFindItem(frame, AccountId(1, SettingsField::AccountSignIn, 1)) &&
              !UiFindItem(frame, AccountId(1, SettingsField::AccountSignOut, 1)));
    });

    Run("accounts page: empty state; a dragged card is lifted and drawn last", [] {
        UiGraphics& g = TestGraphics();
        SettingsView view = TestView(SettingsUiPage::Accounts);
        UiFrame frame;
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK_NEAR(frame.viewport.top, 56);
        CHECK(HasText(frame, L"No accounts yet") && HasText(frame, L"Add account"));
        // Every provider named, with how each signs in.
        CHECK(HasText(frame, L"Anthropic, OpenAI, Google Antigravity, GitHub Copilot or OpenCode Go account"));
        CHECK(HasText(frame, L"OpenCode Go with an API key") && HasText(frame, L"Google Antigravity uses its signed-in app"));
        CHECK(frame.content.front().kind == UiItemKind::Card);
        CHECK_NEAR(frame.content.front().rect.top, 14);
        CHECK(UiFindItem(frame, SettingsControlId(SettingsField::AccountAdd)));

        view = AccountsView();
        std::swap(view.settings.accounts[0], view.settings.accounts[2]);
        std::swap(view.accounts[0], view.accounts[2]);
        view.dragKey = 11;
        view.dragTop = 100;
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        const UiItem* lifted = UiFindItem(frame, AccountId(1, SettingsField::AccountExpand, 1));
        CHECK(lifted && lifted->selected);
        CHECK_NEAR(lifted->rect.top, 100);
        CHECK_NEAR(UiFindItem(frame, AccountId(3, SettingsField::AccountExpand, 1))->rect.top, 14);
        CHECK(frame.content.back().rect.top >= 100 && frame.content.back().rect.bottom <= 100 + 68);
    });

    Run("accounts page: a drag's later reports move the built cards where a rebuild puts them", [] {
        UiGraphics& g = TestGraphics();
        auto build = [&](const SettingsView& view) {
            UiFrame frame;
            SettingsUiBuildFrame(g, view, {1000, 820}, frame);
            return frame;
        };
        auto panel = [](size_t account) { return AccountId(uint32_t(account + 1), SettingsField::AccountExpand, 1); };
        // Nine cards; Work (index 1, its own text boxes included), ChatGPT Pro and Side project
        // expanded. Work is lifted at 100 in its place, then dragged past three cards to 420.
        std::initializer_list<size_t> expanded = {1, 2, 6};
        std::vector<size_t> start = {0, 1, 2, 3, 4, 5, 6, 7, 8}, later = {0, 2, 3, 4, 1, 5, 6, 7, 8};
        float origin = UiFindItem(build(NineAccountsView(start, expanded)), panel(0))->rect.top;
        SettingsView first = NineAccountsView(start, expanded), moved = NineAccountsView(later, expanded);
        first.dragKey = moved.dragKey = 22;
        first.dragTop = 100;
        moved.dragTop = 420;
        UiFrame live = build(first), rebuilt = build(moved);
        std::vector<uint32_t> panels;
        for (size_t account : later) panels.push_back(panel(account));
        CHECK(SettingsMoveAccountCards(live.content, panels, panel(1), origin, 420));
        // Every item where the rebuild puts it (running ids differ: compared by kind, text and box).
        CHECK(live.content.size() == rebuilt.content.size());
        std::vector<bool> used(rebuilt.content.size());
        size_t matched = 0;
        for (const UiItem& item : live.content) {
            for (size_t j = 0; j < rebuilt.content.size(); j++) {
                const UiItem& other = rebuilt.content[j];
                const D2D1_RECT_F &a = item.rect, &b = other.rect;
                if (used[j] || other.kind != item.kind || other.text != item.text ||
                    UiRunsText(other.runs) != UiRunsText(item.runs) || std::abs(a.left - b.left) > 1e-3f ||
                    std::abs(a.top - b.top) > 1e-3f || std::abs(a.right - b.right) > 1e-3f ||
                    std::abs(a.bottom - b.bottom) > 1e-3f) {
                    continue;
                }
                used[j] = true;
                matched++;
                break;
            }
        }
        CHECK(matched == live.content.size());
        // The lifted card (its label box too) at 420 and still drawn last; ChatGPT Pro moved up
        // into Work's place under the first card.
        const UiItem* lifted = UiFindItem(live, panel(1));
        CHECK(lifted->selected && std::abs(lifted->rect.top - 420) < 1e-3f);
        CHECK(live.content.back().rect.top >= 419 && live.content.back().rect.bottom <= lifted->rect.bottom);
        CHECK(UiFindItem(live, AccountId(2, SettingsField::AccountLabel))->rect.top > 420);
        const UiItem* firstCard = UiFindItem(live, panel(0));
        CHECK(std::abs(UiFindItem(live, panel(2))->rect.top - (firstCard->rect.bottom + 4)) < 1e-3f);
        // Reported again where it is: nothing moves.
        CHECK(!SettingsMoveAccountCards(live.content, panels, panel(1), origin, 420));
    });
}
