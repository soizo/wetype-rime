#include <rime_api.h>
#include <dlfcn.h>
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

static RimeApi *api;
static void require(bool ok, const char *message) {
    if (!ok) throw std::runtime_error(message);
}
static std::vector<std::string> rows(RimeSessionId id) {
    RIME_STRUCT(RimeContext, context);
    require(api->get_context(id, &context), "no context");
    std::vector<std::string> words;
    for (int i = 0; i < context.menu.num_candidates; ++i)
        words.emplace_back(context.menu.candidates[i].text);
    api->free_context(&context);
    return words;
}
static auto query(RimeSessionId id, const std::string &code) {
    api->clear_composition(id);
    for (char key : code) api->process_key(id, key, 0);
    return rows(id);
}
static RimeSessionId session(const char *schema) {
    auto id = api->create_session();
    require(id && api->select_schema(id, schema), "schema unavailable");
    api->set_option(id, "ascii_mode", false);
    return id;
}
static std::string read(const std::string &path) {
    std::ifstream file(path);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
static void write(const std::string &path, const std::string &text) {
    std::ofstream file(path);
    file << text;
    file.close();
    require(bool(file), "cannot write YAML fixture");
}
static std::string rules_error(RimeSessionId id) {
    char error[512] = {};
    api->get_property(id, "_wetype_rules_error", error, sizeof(error));
    return error;
}
static void action(RimeSessionId id, char key, int modifiers = 4) {
    require(api->process_key(id, key, modifiers), "shortcut not consumed");
    auto error = rules_error(id);
    require(error.empty(), error.c_str());
}
static bool contains(const std::vector<std::string> &words, const std::string &word) {
    return std::find(words.begin(), words.end(), word) != words.end();
}

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    try {
        require(dlopen("/Library/Input Methods/Squirrel.app/Contents/Frameworks/rime-plugins/librime-lua.dylib",
                       RTLD_NOW | RTLD_GLOBAL), "Lua unavailable");
        api = rime_get_api();
        const char *modules[] = {"default", "lua", nullptr};
        RIME_STRUCT(RimeTraits, traits);
        traits.shared_data_dir = "/Library/Input Methods/Squirrel.app/Contents/SharedSupport";
        traits.user_data_dir = argv[1];
        traits.log_dir = argv[2];
        traits.modules = modules;
        traits.app_name = "rime.wetype-preferences";
        traits.min_log_level = 2;
        api->setup(&traits);
        api->initialize(&traits);
        api->start_maintenance(true);
        api->join_maintenance_thread();

        const std::string path = std::string(argv[1]) + "/wetype-candidate-rules.custom.yaml";
        auto id = session("wetype");
        auto baseline = query(id, "ni");
        require(baseline.size() >= 4, "too few pinyin candidates");
        auto error = rules_error(id);
        require(error.empty(), error.c_str());
        auto initial = read(path);
        require(api->highlight_candidate(id, 1), "highlight failed");
        api->process_key(id, 'p', 4);
        api->process_key(id, 'x', 4);
        api->process_key(id, 'p', 5);
        require(read(path) == initial, "default shortcut wrote rules");

        auto first = baseline[2], second = baseline[1], blocked = baseline[0];
        std::string pins = "# Keep this comment\n- layout: quanpin\n  code: ni\n"
            "  pin: ['" + first + "', '" + second + "'] # Pin order\n";
        write(path, pins + "  block: ['" + first + "', '" + blocked + "']\n");
        auto active = query(id, "ni");
        require(active.size() >= 2 && active[0] == second && !contains(active, blocked) && !contains(active, first),
                "YAML block or pin did not apply on next query");
        write(path, "[\n");
        require(query(id, "ni") == active && !rules_error(id).empty(), "invalid YAML replaced good rules");
        write(path, pins);
        active = query(id, "ni");
        require(active.size() >= 2 && active[0] == first && active[1] == second && rules_error(id).empty(),
                "pin order or unblock failed");
        auto separate = session("wetype_quanpin");
        require(query(separate, "ni") == active, "standalone and unified layout disagree");
        api->destroy_session(separate);
        require(api->process_key(id, ' ', 0), "pinned candidate was not selected");
        RIME_STRUCT(RimeCommit, commit);
        require(api->get_commit(id, &commit) && first == commit.text, "pin changed native selection identity");
        api->free_commit(&commit);
        api->destroy_session(id);

        id = session("wetype_wubi86");
        baseline = query(id, "wq");
        require(baseline.size() >= 3, "too few Wubi candidates");
        auto target = baseline[1];
        require(api->highlight_candidate(id, 1), "highlight failed");
        action(id, 'p');
        require(rows(id)[0] == target && read(path).rfind(pins, 0) == 0, "pin or comment-preserving write failed");
        action(id, 'x');
        require(!contains(rows(id), target), "block did not hide pinned candidate");
        action(id, 'u');
        require(rows(id)[0] == target, "restore did not preserve pin position");
        action(id, 'R', 5);
        require(rows(id) == baseline, "unpin did not restore baseline ordering");
        require(read(path).rfind(pins, 0) == 0, "shortcut rewrote hand-edited YAML");
        api->destroy_session(id);
        api->finalize();
        puts("PASS: YAML reload and fallback, ordered pins, scoped blocks, default-off shortcuts, comment-preserving writes and native commit");
    } catch (const std::exception &error) {
        fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
