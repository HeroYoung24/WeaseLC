#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <rime_api.h>
#include <rime_levers_api.h>
#include "RimeLearningChecks.h"

static bool CheckTranslatorLearning(const char* translator) {
  namespace fs = std::filesystem;
  const auto source = fs::current_path();
  if (!fs::exists(source / "WeaselDeployer" / "WeaselDeployer.vcxproj")) return false;
  const auto root = source / "output" / "RimeLearningTests" /
      (sizeof(void*) == 8 ? "x64" : "Win32") / std::to_string(GetCurrentProcessId()) / translator;
  std::error_code ec;
  // Never reuse a previous learning database or follow a linked output folder.
  for (auto p = root;; p = p.parent_path()) {
    const auto attributes = GetFileAttributesW(p.c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    if (p == source) break;
  }
  if (fs::exists(root)) return false;
  fs::create_directories(root / "shared", ec); if (ec) return false;
  fs::create_directories(root / "user", ec); if (ec) return false;
  std::ofstream report("RimeLearningChecks.log", std::ios::app);
  report << "translator = " << translator << '\n';
  bool valid = true;
  auto check = [&](bool ok, const char* name) { report << name << " = " << ok << '\n'; report.flush(); valid = valid && ok; return ok; };
  auto write = [&](const char* name, const std::string& text) { std::ofstream out(root / "shared" / name, std::ios::binary); out << text; return !!out; };
  if (!write("default.yaml", "config_version: '1'\nschema_list:\n  - schema: weasel_learning_probe\n") ||
      !write("weasel_learning_probe.schema.yaml",
          "schema:\n  schema_id: weasel_learning_probe\n  name: Synthetic learning test\n  version: '1'\n"
          "engine:\n  processors: [ascii_composer, punctuator, speller, selector, express_editor]\n"
          "  segmentors: [ascii_segmentor, abc_segmentor, punct_segmentor, fallback_segmentor]\n  translators: [punct_translator, " + std::string(translator) + "]\n"
          "speller:\n  alphabet: abcxyz\ntranslator:\n  dictionary: weasel_learning_probe\n"
          "  enable_user_dict: true\n  enable_sentence: false\npunctuator:\n  half_shape:\n    \".\": {commit: \"。\"}\nmenu:\n  page_size: 9\n") ||
      !write("weasel_learning_probe.dict.yaml", "---\nname: weasel_learning_probe\nversion: '1'\nsort: by_weight\n"
          "use_preset_vocabulary: false\n...\n默认词\taa\t100000\n习惯词\taa\t1\n")) return false;
  const auto shared = (root / "shared").u8string(), user = (root / "user").u8string(), logs = root.u8string();
  RIME_STRUCT(RimeTraits, traits);
  traits.shared_data_dir = shared.c_str(); traits.user_data_dir = user.c_str();
  traits.app_name = "rime.weasel_learning_test"; traits.log_dir = logs.c_str(); traits.min_log_level = 2;
  auto* api = rime_get_api();
  api->setup(&traits); api->deployer_initialize(&traits);
  const auto schema = (root / "shared" / "weasel_learning_probe.schema.yaml").u8string();
  if (!check(api->deploy_config_file("default.yaml", "config_version") && api->deploy_schema(schema.c_str()),
      "isolated tiny schema deployed")) { api->finalize(); return false; }
  api->initialize(&traits);
  auto candidates = [&](RimeSessionId session) {
    std::vector<std::string> items;
    RIME_STRUCT(RimeContext, context);
    if (api->get_context(session, &context)) {
      for (int i = 0; i < context.menu.num_candidates; ++i) items.emplace_back(context.menu.candidates[i].text);
      api->free_context(&context);
    }
    return items;
  };
  auto session = api->create_session();
  if (!check(session && api->select_schema(session, "weasel_learning_probe"), "isolated Rime session ready")) { api->finalize(); return false; }
  api->simulate_key_sequence(session, "aa");
  auto initial = candidates(session);
  check(initial.size() == 2 && initial.front() == "默认词", "baseline dictionary frequency determines first candidate");
  api->clear_composition(session);
  for (unsigned i = 0; i < 5; ++i) {
    api->simulate_key_sequence(session, "aa");
    auto items = candidates(session);
    auto at = std::find(items.begin(), items.end(), "习惯词");
    bool committed = at != items.end() && api->select_candidate(session, static_cast<std::size_t>(at - items.begin()));
    RIME_STRUCT(RimeCommit, commit);
    bool got = api->get_commit(session, &commit);
    if (!got) { api->commit_composition(session); got = api->get_commit(session, &commit); }
    committed = got && commit.text && std::string(commit.text) == "习惯词" && committed;
    if (got) api->free_commit(&commit);
    if (!check(committed, "synthetic choice committed")) break;
  }
  api->destroy_session(session); api->finalize();
  // Reload from disk with the same isolated paths; no in-memory ranking remains.
  api->initialize(&traits); session = api->create_session();
  check(session && api->select_schema(session, "weasel_learning_probe"), "learning database reloaded");
  api->simulate_key_sequence(session, "aa");
  auto learned = candidates(session);
  check(!learned.empty() && learned.front() == "习惯词", "learned phrase ranks first after reload");
  api->destroy_session(session); api->finalize();
  api->deployer_initialize(&traits);
  auto module = api->find_module("levers");
  const auto exported = (root / "learned.txt").u8string();
  auto* levers = module ? reinterpret_cast<RimeLeversApi*>(module->get_api()) : nullptr;
  check(levers && levers->export_user_dict("weasel_learning_probe", exported.c_str()) > 0, "persisted frequency exported from isolated user dictionary");
  std::ifstream dump(root / "learned.txt", std::ios::binary);
  std::string line; bool count = false;
  while (std::getline(dump, line)) if (line == "习惯词\taa\t5" || line == "习惯词\taa\t5\r") count = true;
  check(count, "user dictionary records five explicit commits");
  api->finalize();
  api->initialize(&traits);
  auto punctuation = api->create_session();
  check(punctuation && api->select_schema(punctuation, "weasel_learning_probe"), "punctuation fixture uses only its synthetic schema");
  auto feed = [&](const std::string& keys) {
    std::string text;
    for (const unsigned char key : keys) {
      const bool eaten = api->process_key(punctuation, key, 0) != False;
      RIME_STRUCT(RimeCommit, commit);
      if (api->get_commit(punctuation, &commit)) { if (commit.text) text += commit.text; api->free_commit(&commit); }
      if (!eaten) text += static_cast<char>(key);
    }
    return text;
  };
  const auto firstDot = feed("1.");
  const char* input = api->get_input(punctuation);
  check(firstDot == "1" && input && std::string(input) == ".", "first numeric dot remains in native composition for inline display");
  check(feed(".") == "。" && (api->get_input(punctuation) && !*api->get_input(punctuation)), "second dot commits one Chinese period without Enter");
  check(feed("3.14 ") == "3.14 ", "native punctuation preserves decimal input");
  check(feed("1.") == "1", "new numeric dot can start after earlier punctuation");
  api->clear_composition(punctuation);
  check((api->get_input(punctuation) && !*api->get_input(punctuation)) && feed("2 ") == "2 ", "cancelled numeric punctuation never swallows following input");
  api->destroy_session(punctuation); api->finalize();
  return valid;
}
bool CheckRimeLearning() {
  std::ofstream("RimeLearningChecks.log", std::ios::trunc).close();
  bool valid = true;
  for (const auto* translator : {"table_translator", "script_translator"})
    valid = CheckTranslatorLearning(translator) && valid;
  return valid;
}
