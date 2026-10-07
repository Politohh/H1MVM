#include "component/depot_store.hpp"
#include <cassert>
#include <iostream>

int main()
{
    const auto root = std::filesystem::temp_directory_path() /
        ("h1-depot-test-" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count()));
    const auto path = root / "profile" / "depot.json";
    assert(depot_store::load(path) == depot_store::defaults());
    auto state = depot_store::defaults();
    state["currencies"]["parts"] = 2500;
    state["items"]["head_guid"] = true;
    state["items"]["crate"] = 3;
    state["items"]["not_owned"] = false;
    state["has_seen_mod_eula"] = true;
    state["extension"] = "retained";
    depot_store::save(path, state.dump());
    assert(depot_store::load(path) == state);
    state["currencies"]["parts"] = 2400;
    depot_store::save(path, state.dump());
    assert(depot_store::read(path.string() + ".bak")["currencies"]["parts"] == 2500);
    const auto saved = depot_store::load(path);
    for (const auto& bad : {R"([])", R"({"currencies":{"parts":-1}})",
        R"({"items":{"head":{}}})", R"({"currencies":{"parts":1.5}})", R"({"has_seen_mod_eula":4})"})
    {
        bool failed = false;
        try { depot_store::save(path, bad); }
        catch (const std::exception&) { failed = true; }
        assert(failed && depot_store::load(path) == saved);
    }
    const auto legacy = depot_store::normalize(nlohmann::json::parse(R"({"items":[],"reward_splashes":[],"currencies":{}})"));
    assert(legacy == depot_store::defaults());
    // Profiles are independent; invalid original data is preserved on recovery.
    const auto second = root / "mod" / "depot.json";
    depot_store::save(second, depot_store::defaults().dump());
    assert(depot_store::load(path) == saved && depot_store::load(second) == depot_store::defaults());
    { std::ofstream corrupt(path); corrupt << "unfinished{"; }
    bool failed = false;
    try { (void)depot_store::load(path); } catch (const std::exception&) { failed = true; }
    assert(failed);
    depot_store::save(path, state.dump());
    bool preserved = false;
    for (const auto& file : std::filesystem::directory_iterator(path.parent_path()))
        if (file.path().filename().string().starts_with("depot.json.corrupt."))
        {
            std::ifstream stream(file.path()); std::string content; stream >> content;
            preserved = content == "unfinished{";
        }
    assert(preserved && depot_store::load(path) == state);
    // Only this uniquely named test directory is disposable.
    std::filesystem::remove_all(root);
    std::cout << "PASS Depot persistence, validation, profile separation and recovery\n";
}
