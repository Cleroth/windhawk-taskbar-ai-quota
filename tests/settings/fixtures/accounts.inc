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
// CC 2 signed in (Ultra), Go with its local app running (AI Pro). Each with a fresh weekly reading
// taken at 14:32: OAI 62%, CC 1 40% (a card that needs a sign-in shows none), CC 2 93%, Go 12%.
static SettingsView AccountsView(std::initializer_list<int> expanded = {}) {
    SettingsView view = TestView(SettingsUiPage::Accounts);
    AddTestAccounts(view.settings);
    const SettingsAccountStatus statuses[] = {
        {L"Plus", {}, true, true, false, false, {kWeeklyBar, 62, false}, L"14:32"},
        {L"Pro", {AccountErrorCode::NotSignedIn}, false, false, true, false, {kWeeklyBar, 40, false}, L"14:32"},
        {L"Ultra", {}, true, true, false, false, {kWeeklyBar, 93, false}, L"14:32"},
        {L"AI Pro", {}, true, false, false, false, {kWeeklyBar, 12, false}, L"14:32"},
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

// The usage a card's 68 DIP header shows: its percentage (or dash), the header's only
// right-aligned text, and its meter; null where it shows none.
struct TestCardUsage {
    const UiItem* percent = nullptr;
    const UiItem* meter = nullptr;
};

static TestCardUsage CardUsage(const UiFrame& frame, uint32_t slot) {
    const UiItem* header = UiFindItem(frame, AccountId(slot, SettingsField::AccountExpand));
    if (!header) throw std::runtime_error("missing card " + std::to_string(slot));
    TestCardUsage usage;
    for (const UiItem& item : frame.content) {
        if (item.rect.top < header->rect.top || item.rect.bottom > header->rect.bottom) continue;
        if (item.kind == UiItemKind::Meter) usage.meter = &item;
        if (item.kind == UiItemKind::Text && item.align == DWRITE_TEXT_ALIGNMENT_TRAILING) usage.percent = &item;
    }
    return usage;
}

// Nine accounts of every provider with long labels, plans and errors, some with readings (fresh,
// stale, past 100%): history IDs (keys) 21-29, slots 1-9, cards in `order` (indexes into that
// list), the listed ones expanded.
static SettingsView NineAccountsView(std::vector<size_t> order, std::initializer_list<size_t> expanded) {
    struct Entry {
        PCWSTR provider, label;
        SettingsAccountStatus status;
    };
    const Entry entries[] = {
        {L"anthropic", L"Claude Max (personal)", {L"Ultra", {}, true, true, false, false, {kWeeklyBar, 112, false}, L"14:32"}},
        {L"anthropic", L"Work", {L"Team", {}, true, true, false, false, {kWeeklyBar, 47, false}, L"14:32"}},
        {L"openai", L"ChatGPT Pro", {L"Pro", {}, true, true, false, false}},
        {L"anthropic", L"Old login",
         {L"Pro", {AccountErrorCode::NotSignedIn}, false, false, true, false}},
        {L"antigravity", L"Gemini desktop", {L"AI Pro", {}, true, false, false, false, {kWeeklyBar, 8, false}, L"14:32"}},
        {L"openai", L"Codex (team seat)", {L"Plus", {}, true, true, false, false}},
        {L"anthropic", L"Side project",
         {L"Max", {AccountErrorCode::Message, L"HTTP 429: rate limited, retrying in 5 minutes"}, true, true, false,
          false, {kWeeklyBar, 81, true}, L"Tue 09:05"}},
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
        CHECK(ErrorText(*DraftOf(edits, 12, kLabel)) == L"Enter a label.");
        edits.SetText(12, kLabel, L"CC 2");
        CHECK(ErrorText(*DraftOf(edits, 12, kLabel)).empty());  // Typing clears it
        CHECK(!edits.RequestCommit(12, kLabel, true) && edits.queue.empty());
        CHECK(ErrorText(*DraftOf(edits, 12, kLabel)) == L"Another account of this provider uses this label.");
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
        CHECK(AccountOps(store).empty() && ErrorText(*DraftOf(edits, 12, kLabel)) == L"Enter a label.");
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
        CHECK(failed && failed->failed && ErrorText(*failed) == L"This change couldn't be saved.");
        CHECK(edits.notices.size() == 1 && edits.notices[0].failure && edits.notices[0].code == SettingsNoticeCode::AccountSaveFailed);
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
        CHECK(ErrorText(*DraftOf(edits, 12, kLabel)) == L"Another account of this provider uses this label.");
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
        CHECK(HasText(frame, L"Anthropic \u00B7 Ultra \u00B7 "));
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
        CHECK(HasText(frame, L"OpenAI \u00B7 Plus \u00B7 Hidden, polled every hour"));
    });

    Run("accounts page: each card's usage left of its switch, at the same spot on every card: the percentage in "
        "the room of 100% after a 48 DIP meter; a dash without a reading; neither where a sign-in is needed; no "
        "meter on a card under 560 DIP", [] {
        UiGraphics& g = TestGraphics();
        SettingsView view = AccountsView();
        view.accounts[3].status.usage = {};  // Go: no reading
        const float room = TestTextWidth(L"100%", UiFont::Note);
        auto lastChip = [](const UiFrame& frame, uint32_t slot) {
            const UiItem* header = UiFindItem(frame, AccountId(slot, SettingsField::AccountExpand));
            float right = 0;
            for (const UiItem& item : frame.content) {
                if (item.kind == UiItemKind::Chip && item.rect.top >= header->rect.top && item.rect.bottom <= header->rect.bottom) {
                    right = std::max(right, item.rect.right);
                }
            }
            return right;
        };
        auto index = [](const UiFrame& frame, const UiItem* item) { return item - frame.content.data(); };
        UiFrame frame;
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        // 680 DIP cards from 20: 17 DIP in, the 16 DIP chevron, 14, the 40 DIP switch, 14, the
        // percentage, 8, the meter, then the quota keys 14 before it.
        const float percentRight = 700 - 17 - 16 - 14 - 40 - 14, meterRight = percentRight - room - 8;
        const std::pair<uint32_t, PCWSTR> shown[] = {{1, L"62%"}, {3, L"93%"}, {4, L"\u2014"}};
        for (auto [slot, text] : shown) {
            TestCardUsage usage = CardUsage(frame, slot);
            float cy = 14 + 72.0f * float(slot - 1) + 34;
            CHECK(usage.percent && usage.percent->font == UiFont::Note && UiRunsText(usage.percent->runs) == text &&
                  !usage.percent->trim);
            CHECK_NEAR(usage.percent->rect.right, percentRight);
            CHECK_NEAR(usage.percent->rect.left, percentRight - room);
            CHECK_NEAR(usage.percent->rect.top, cy - 10);
            CHECK_NEAR(usage.percent->rect.bottom, cy + 10);
            // The room stays where no meter shows: every card's keys end at the same spot.
            CHECK_NEAR(lastChip(frame, slot), meterRight - 48 - 14);
            const UiItem* visible = UiFindItem(frame, AccountId(slot, SettingsField::AccountHidden));
            CHECK_NEAR(visible->rect.left, percentRight + 14);
            CHECK((usage.meter != nullptr) == (slot != 4));
            if (!usage.meter) {
                // The dash: the muted ink, no color.
                CHECK(usage.percent->ink == UiInk::Muted && !usage.percent->runs[0].color);
                continue;
            }
            CHECK_NEAR(usage.meter->rect.right, meterRight);
            CHECK_NEAR(usage.meter->rect.left, meterRight - 48);
            CHECK_NEAR(usage.meter->rect.top, cy - 8);
            CHECK_NEAR(usage.meter->rect.bottom, cy + 8);
            CHECK(usage.meter->minimum == 0 && usage.meter->maximum == 100 && usage.meter->enabled);
            // In reading order: the keys, the meter, the percentage, the switch.
            CHECK(index(frame, usage.meter) < index(frame, usage.percent) && index(frame, usage.percent) < index(frame, visible));
            // Neither takes the pointer or the focus: the header under them does.
            for (const UiItem* item : {usage.meter, usage.percent}) {
                D2D1_POINT_2F center{(item->rect.left + item->rect.right) / 2, (item->rect.top + item->rect.bottom) / 2};
                CHECK(UiHitTest(g, frame.content, center) == UiFocusRef{AccountId(slot, SettingsField::AccountExpand)});
            }
        }
        CHECK(UiFocusStops(frame).size() == 6 + 1 + 4 * 2 + 1);
        // CC 1 needs a sign-in: no usage, its keys 14 DIP before the switch, Sign in before them.
        TestCardUsage none = CardUsage(frame, 2);
        CHECK(!none.percent && !none.meter);
        CHECK_NEAR(lastChip(frame, 2), percentRight);
        CHECK(UiFindItem(frame, AccountId(2, SettingsField::AccountSignIn))->rect.right < percentRight - room);
        // While its sign-in window is open the button is gone and its reading shows.
        view.accounts[1].status.signingIn = true;
        frame = {};
        SettingsUiBuildFrame(g, view, {1000, 820}, frame);
        CHECK(UiRunsText(CardUsage(frame, 2).percent->runs) == L"40%" && CardUsage(frame, 2).meter &&
              !UiFindItem(frame, AccountId(2, SettingsField::AccountSignIn)));
        view.accounts[1].status.signingIn = false;

        // A 560 DIP card (an 880 DIP client) still has the meter, a 559 DIP one only the
        // percentage, its keys 14 DIP before it; so the window's minimum, 464 DIP. The card is the
        // client less the 260 DIP pane and 60 DIP around it, from 20; the percentage ends 101 DIP
        // before its edge.
        for (float client : {880.0f, 879.0f, 784.0f}) {
            frame = {};
            SettingsUiBuildFrame(g, view, {client, 820}, frame);
            const float right = 20 + (client - 320) - 101;
            const bool meters = client == 880;
            for (uint32_t slot : {1u, 3u, 4u}) {
                TestCardUsage usage = CardUsage(frame, slot);
                CHECK(usage.percent && (usage.meter != nullptr) == (meters && slot != 4));
                CHECK_NEAR(usage.percent->rect.right, right);
                CHECK_NEAR(usage.percent->rect.left, right - room);
                CHECK_NEAR(lastChip(frame, slot), right - room - (meters ? 8 + 48 : 0) - 14);
                if (usage.meter) CHECK_NEAR(usage.meter->rect.right - usage.meter->rect.left, 48);
            }
            CHECK(!CardUsage(frame, 2).percent && !CardUsage(frame, 2).meter);
        }
    });

    Run("accounts page: a fresh reading in its usage color, as text in the theme's variant; a stale one muted "
        "with a grey meter; used or left as the taskbar shows it, rounded, the meter within 0 to 100", [] {
        UiGraphics& g = TestGraphics();
        auto usageOf = [&](const SettingsView& view, uint32_t slot) {
            UiFrame frame;
            SettingsUiBuildFrame(g, view, {1000, 820}, frame);
            TestCardUsage usage = CardUsage(frame, slot);
            if (!usage.percent || !usage.meter) throw std::runtime_error("no usage shown");
            // Text, the percentage's run color (0: its ink alone), its ink; the meter's value and color.
            struct Shown {
                std::wstring text;
                uint32_t textColor;
                UiInk ink;
                int value;
                uint32_t color;
            };
            const UiRun& run = usage.percent->runs.at(0);
            return Shown{run.text, run.color ? UiColorKey(*run.color) : 0, usage.percent->ink, usage.meter->action.value,
                         usage.meter->color ? UiColorKey(*usage.meter->color) : 0};
        };
        auto contrast = [](uint32_t rgb) { return 1.05 / (RelativeLuminance(rgb & 0xFFFFFF) + 0.05); };
        SettingsView view = AccountsView();
        // By the default thresholds (50, 75, 90): OAI's 62% yellow, CC 2's 93% red, Go's 12% green.
        // Dark and high contrast: the color itself; light: red and green as the usage history
        // window's sidebar shows them, yellow darkened until it reads on white. The meter keeps
        // the usage color in every theme.
        for (UiThemeKind theme : {UiThemeKind::Dark, UiThemeKind::HighContrast, UiThemeKind::Light}) {
            view.theme = theme;
            const bool light = theme == UiThemeKind::Light;
            auto red = usageOf(view, 3), green = usageOf(view, 4), yellow = usageOf(view, 1);
            CHECK(red.text == L"93%" && red.ink == UiInk::Primary && red.value == 93 && red.color == 0xFFE53935);
            CHECK(red.textColor == (light ? 0xFFC42B1C : 0xFFE53935));
            CHECK(green.text == L"12%" && green.ink == UiInk::Primary && green.value == 12 && green.color == 0xFF43A047);
            CHECK(green.textColor == (light ? 0xFF1D7A35 : 0xFF43A047));
            CHECK(yellow.text == L"62%" && yellow.value == 62 && yellow.color == 0xFFFDD835);
            CHECK(light ? yellow.textColor != 0xFFFDD835 && contrast(yellow.textColor) >= 4.5 : yellow.textColor == 0xFFFDD835);
            // Stale: the muted ink without a color, a grey meter, whatever the theme.
            SettingsView old = view;
            old.accounts[2].status.usage.stale = true;
            auto stale = usageOf(old, 3);
            CHECK(stale.text == L"93%" && stale.ink == UiInk::Muted && !stale.textColor && stale.value == 93 &&
                  stale.color == 0xFF9E9E9E);
        }
        // The settings' thresholds and palette: 12% is yellow from 10; the colorblind blue reads on
        // white as it is; a custom color (0xBBGGRR) below yellow.
        view.theme = UiThemeKind::Light;
        view.settings.yellowThreshold = 10;
        CHECK(usageOf(view, 4).color == 0xFFFDD835);
        view.settings.yellowThreshold = 50;
        view.settings.usagePalette = UsagePalette::Colorblind;
        CHECK(usageOf(view, 4).color == 0xFF0072B2 && usageOf(view, 4).textColor == 0xFF0072B2);
        CHECK(usageOf(view, 3).color == 0xFFD55E00);
        view.settings.usagePalette = UsagePalette::Custom;
        view.settings.customUsageColors[0] = RGB(0x10, 0x20, 0x30);
        CHECK(usageOf(view, 4).color == 0xFF102030 && usageOf(view, 4).textColor == 0xFF102030);
        view.settings.usagePalette = UsagePalette::Default;

        // Remaining mode: what is left, in the used share's color.
        view.theme = UiThemeKind::Dark;
        view.settings.barMode = BarMode::Remaining;
        auto left = usageOf(view, 1);
        CHECK(left.text == L"38%" && left.value == 38 && left.color == 0xFFFDD835 && left.textColor == 0xFFFDD835);
        CHECK(usageOf(view, 3).text == L"7%" && usageOf(view, 3).color == 0xFFE53935);
        // Past 100% used: nothing left, an empty meter; used, the number as read and a full meter.
        view.accounts[2].status.usage.pct = 112.4;
        CHECK(usageOf(view, 3).text == L"0%" && usageOf(view, 3).value == 0);
        view.settings.barMode = BarMode::Used;
        CHECK(usageOf(view, 3).text == L"112%" && usageOf(view, 3).value == 100 && usageOf(view, 3).color == 0xFFE53935);
        // Whole percents, rounded.
        view.accounts[2].status.usage.pct = 41.6;
        CHECK(usageOf(view, 3).text == L"42%" && usageOf(view, 3).value == 42);
        view.accounts[2].status.usage.pct = 0.4;
        CHECK(usageOf(view, 3).text == L"0%" && usageOf(view, 3).value == 0);
        view.settings.barMode = BarMode::Remaining;
        CHECK(usageOf(view, 3).text == L"100%" && usageOf(view, 3).value == 100);
    });

    Run("accounts page: the header's tooltip shows for a stale reading and names its quota and when it was "
        "read; a tooltip for cut text ends with the same line; none without a reading", [] {
        UiGraphics& g = TestGraphics();
        auto tooltip = [&](const SettingsView& view, uint32_t slot) {
            UiFrame frame;
            SettingsUiBuildFrame(g, view, {1000, 820}, frame);
            return UiFindItem(frame, AccountId(slot, SettingsField::AccountExpand))->tooltip;
        };
        SettingsView view = AccountsView();
        for (uint32_t slot = 1; slot <= 4; slot++) CHECK(tooltip(view, slot).empty());
        // CC 2's reading out of date: its label (strong), its details, then the reading.
        view.accounts[2].status.usage.stale = true;
        std::vector<UiRun> stale = tooltip(view, 3);
        CHECK(UiRunsText(stale) == L"CC 2\nAnthropic \u00B7 Ultra \u00B7 Signed in\n7d: 93% used, read 14:32");
        CHECK(stale.at(0).text == L"CC 2" && stale[0].style == UiRunStyle::Strong);
        for (uint32_t slot : {1u, 2u, 4u}) CHECK(tooltip(view, slot).empty());
        // Read on another day, or at a time that couldn't be told.
        view.accounts[2].status.usageTime = L"Tue 09:05";
        CHECK(UiRunsText(tooltip(view, 3)).ends_with(L"\n7d: 93% used, read Tue 09:05"));
        view.accounts[2].status.usageTime.clear();
        CHECK(UiRunsText(tooltip(view, 3)).ends_with(L"\n7d: 93% used, read ?"));
        view.accounts[2].status.usageTime = L"14:32";
        // Remaining mode: what is left.
        view.settings.barMode = BarMode::Remaining;
        CHECK(UiRunsText(tooltip(view, 3)).ends_with(L"\n7d: 7% left, read 14:32"));
        view.settings.barMode = BarMode::Used;
        // The quota's key: Fable's, Antigravity's Argon, GitHub Copilot's month.
        view.accounts[2].status.usage.bar = kFableWeeklyBar;
        CHECK(UiRunsText(tooltip(view, 3)).ends_with(L"\nFa: 93% used, read 14:32"));
        view.accounts[3].status.usage = {kFableWeeklyBar, 12, true};
        CHECK(UiRunsText(tooltip(view, 4)) == L"Go\nAntigravity \u00B7 AI Pro \u00B7 Local app running\nAr: 12% used, read 14:32");
        view.settings.accounts[0].provider = L"copilot";
        view.accounts[0].status.usage = {kExtraUsageBar, 62, true};
        CHECK(UiRunsText(tooltip(view, 1)) == L"OAI\nGitHub Copilot \u00B7 Plus \u00B7 Signed in\nMo: 62% used, read 14:32");
        // A stale state without a reading, or on a card that needs a sign-in: no tooltip.
        view.accounts[3].status.usage = {kWeeklyBar, -1, true};
        view.accounts[1].status.usage.stale = true;
        CHECK(tooltip(view, 4).empty() && tooltip(view, 2).empty());

        // A fresh reading: its line only once the tooltip shows, here for a label cut before the
        // controls; a cut card without a reading has the two lines alone.
        view = AccountsView();
        const std::wstring label = L"The shared seat of the research and platform teams, renewed every March by procurement";
        view.settings.accounts[0].label = label;
        CHECK(UiRunsText(tooltip(view, 1)) == label + L"\nOpenAI \u00B7 Plus \u00B7 Signed in\n7d: 62% used, read 14:32");
        view.accounts[0].status.usage = {};
        CHECK(UiRunsText(tooltip(view, 1)) == label + L"\nOpenAI \u00B7 Plus \u00B7 Signed in");
    });

    Run("accounts page: a hidden account's state says how often it is polled, in hours when they are whole; "
        "not polled only while hidden accounts aren't; the Record history row and the header's note follow", [] {
        UiGraphics& g = TestGraphics();
        // OAI (cloud, its card expanded) and Go (Antigravity's local app) hidden; cards wide enough
        // for every state's whole line.
        SettingsView view = AccountsView({0});
        view.settings.accounts[0].hidden = view.settings.accounts[3].hidden = true;
        auto build = [&](int hiddenMinutes, int pollMinutes = 10) {
            view.settings.hiddenPollMinutes = hiddenMinutes;
            view.settings.pollMinutes = pollMinutes;
            UiFrame frame;
            SettingsUiBuildFrame(g, view, {1300, 820}, frame);
            return frame;
        };
        struct StateCase {
            int hidden, poll;
            PCWSTR cloud, local;
        };
        for (const StateCase& c : {StateCase{60, 10, L"Hidden, polled every hour", L"Hidden, polled every hour"},
                                   StateCase{120, 10, L"Hidden, polled every 2 hours", L"Hidden, polled every 2 hours"},
                                   StateCase{1440, 10, L"Hidden, polled every 24 hours", L"Hidden, polled every 24 hours"},
                                   StateCase{30, 10, L"Hidden, polled every 30 minutes", L"Hidden, polled every 30 minutes"},
                                   StateCase{90, 10, L"Hidden, polled every 90 minutes", L"Hidden, polled every 90 minutes"},
                                   // Never more often than visible ones: the cloud interval, Antigravity's minute.
                                   StateCase{5, 10, L"Hidden, polled every 10 minutes", L"Hidden, polled every 5 minutes"},
                                   StateCase{60, 120, L"Hidden, polled every 2 hours", L"Hidden, polled every hour"},
                                   StateCase{kHiddenPollAsShown, 10, L"Hidden, polled every 10 minutes", L"Hidden, polled every minute"},
                                   StateCase{kHiddenPollAsShown, 60, L"Hidden, polled every hour", L"Hidden, polled every minute"},
                                   StateCase{kHiddenPollNever, 10, L"Hidden, not polled", L"Hidden, not polled"}}) {
            UiFrame frame = build(c.hidden, c.poll);
            CHECK(TextCount(frame, std::wstring(L"OpenAI \u00B7 Plus \u00B7 ") + c.cloud) == 1);
            CHECK(TextCount(frame, std::wstring(L"Antigravity \u00B7 AI Pro \u00B7 ") + c.local) == 1);
            // The visible cards keep their states; the hidden ones keep their readings.
            CHECK(HasText(frame, L"Anthropic \u00B7 Ultra \u00B7 Signed in"));
            CHECK(UiRunsText(CardUsage(frame, 1).percent->runs) == L"62%" && CardUsage(frame, 4).meter);
            // Record history on the expanded hidden card: paused only where it isn't polled.
            CHECK(HasText(frame, L"\u25CF Paused while hidden") == (c.hidden == kHiddenPollNever));
            CHECK(HasText(frame, L"\u25CF Recording") == (c.hidden != kHiddenPollNever));
        }
        // The state in the secondary ink, after the provider and plan in the line's own.
        UiFrame frame = build(60);
        auto line = std::find_if(frame.content.begin(), frame.content.end(), [](const UiItem& item) {
            return UiRunsText(item.runs) == L"OpenAI \u00B7 Plus \u00B7 Hidden, polled every hour";
        });
        CHECK(line != frame.content.end() && line->runs.back().text == L"Hidden, polled every hour" &&
              line->runs.back().ink == UiInk::Secondary);
        // Polled whether or not it records: recording off for this user, or for the account.
        view.settings.historyEnabled = false;
        CHECK(HasText(build(60), L"OpenAI \u00B7 Plus \u00B7 Hidden, polled every hour"));
        view.settings.historyEnabled = true;
        view.settings.accounts[0].recordHistory = false;
        CHECK(HasText(build(60), L"OpenAI \u00B7 Plus \u00B7 Hidden, polled every hour"));
        view.settings.accounts[0].recordHistory = true;
        // A failed poll shows its error instead; not polled, nothing fails.
        view.accounts[0].status.error = {AccountErrorCode::Message, L"HTTP 503"};
        CHECK(HasText(build(60), L"OpenAI \u00B7 Plus \u00B7 HTTP 503"));
        CHECK(HasText(build(kHiddenPollNever), L"OpenAI \u00B7 Plus \u00B7 Hidden, not polled"));
        view.accounts[0].status.error = {};
        // Visible, the setting changes neither its state nor its recording.
        view.settings.accounts[0].hidden = false;
        frame = build(kHiddenPollNever);
        CHECK(HasText(frame, L"OpenAI \u00B7 Plus \u00B7 Signed in") && HasText(frame, L"\u25CF Recording"));

        // The pinned header's note: hidden accounts aren't polled; are polled less often (an
        // interval longer than the poll interval); or nothing about polling.
        const std::wstring note = L"Drag to reorder. Toggle hides an account from the taskbar";
        struct NoteCase {
            int hidden, poll;
            PCWSTR end;
        };
        for (const NoteCase& c : {NoteCase{kHiddenPollNever, 10, L"; hidden accounts are not polled."},
                                  NoteCase{60, 10, L"; hidden accounts are polled less often."},
                                  NoteCase{11, 10, L"; hidden accounts are polled less often."},
                                  NoteCase{10, 10, L"."}, NoteCase{5, 10, L"."}, NoteCase{60, 60, L"."},
                                  NoteCase{60, 120, L"."}, NoteCase{kHiddenPollAsShown, 10, L"."}}) {
            frame = build(c.hidden, c.poll);
            const UiItem* shown = UiFindItem(frame, 101);
            CHECK(shown && shown->font == UiFont::Note && shown->ink == UiInk::Muted && UiRunsText(shown->runs) == note + c.end);
        }
        // With every account visible it reads the same.
        view.settings.accounts[3].hidden = false;
        CHECK(UiRunsText(UiFindItem(build(60), 101)->runs) == note + L"; hidden accounts are polled less often.");
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
        view.drafts.push_back({11, kLabel, 1, true, L"", 0, false, false, SettingsDraftError::LabelEmpty});
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
