#ifdef _WIN32
#include <io.h>
#endif
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace fs = std::filesystem;

struct Config {
    std::string keil_install_path;
    std::string keil_cmsis_path;
    std::string keil_armcc_include;
    std::string keil_armclang_include;
    std::string iar_install_path;
    std::string iar_cmsis_path;
    std::string iar_c_include;
    std::string iar_arm_install_path;
    std::string iar_arm_cmsis_path;
    std::string iar_arm_c_include;
    std::string iar_rl78_install_path;
    std::string iar_rl78_c_include;
    std::string iar_rl78_inc;
};

struct Entry {
    std::string compiler;
    std::string file;
    std::vector<std::string> args;
};

struct ProjectData {
    std::string toolchain;
    std::vector<std::string> includes;
    std::vector<std::string> defines;
    std::vector<std::string> sources;
};

static std::string replace_all(std::string s, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

static std::string trim(const std::string& s) {
    size_t first = 0;
    while (first < s.size() && std::isspace(static_cast<unsigned char>(s[first]))) first++;
    size_t last = s.size();
    while (last > first && std::isspace(static_cast<unsigned char>(s[last - 1]))) last--;
    return s.substr(first, last - first);
}

static std::string lower_copy(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

static std::string normalize_name(const std::string& s) {
    return lower_copy(trim(s));
}

static std::string slash(const fs::path& p) {
    return replace_all(p.generic_string(), "\\", "/");
}

static std::string read_file(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path.string());
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void write_text(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("cannot write " + path.string());
    out << text;
}

static std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        item = trim(item);
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

static std::vector<std::string> unique(std::vector<std::string> values) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (auto& v : values) {
        std::string key = replace_all(v, "\\", "/");
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c) { return std::tolower(c); });
        if (seen.insert(key).second) out.push_back(v);
    }
    return out;
}

static fs::path config_path() {
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    fs::path base = appdata ? fs::path(appdata) : fs::path(std::getenv("USERPROFILE") ? std::getenv("USERPROFILE") : ".");
    return base / "KeilFormat" / "config.json";
#else
    const char* xdg_config_home = std::getenv("XDG_CONFIG_HOME");
    if (xdg_config_home && *xdg_config_home) {
        return fs::path(xdg_config_home) / "KeilFormat" / "config.json";
    }
    const char* home = std::getenv("HOME");
    return fs::path(home ? home : ".") / ".config" / "KeilFormat" / "config.json";
#endif
}

static std::string json_value(const std::string& json, const std::string& key) {
    std::regex re("\\\"" + key + "\\\"\\s*:\\s*\\\"([^\\\"]*)\\\"");
    std::smatch m;
    return std::regex_search(json, m, re) ? m[1].str() : "";
}

static std::string json_object(const std::string& json, const std::string& key) {
    std::regex re("\\\"" + key + "\\\"\\s*:\\s*\\{([\\s\\S]*?)\\}");
    std::smatch m;
    return std::regex_search(json, m, re) ? m[1].str() : "";
}

static std::string json_escape_value(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char ch : s) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += ch; break;
        }
    }
    return out;
}

static Config load_config() {
    Config c;
    fs::path p = config_path();
    if (!fs::exists(p)) return c;
    std::string json = read_file(p);
    c.keil_install_path = json_value(json, "keil_install_path");
    c.keil_cmsis_path = json_value(json, "keil_cmsis_path");
    c.keil_armcc_include = json_value(json, "keil_armcc_include");
    c.keil_armclang_include = json_value(json, "keil_armclang_include");
    c.iar_install_path = json_value(json, "iar_install_path");
    c.iar_cmsis_path = json_value(json, "iar_cmsis_path");
    c.iar_c_include = json_value(json, "iar_c_include");
    c.iar_arm_install_path = json_value(json, "iar_arm_install_path");
    c.iar_arm_cmsis_path = json_value(json, "iar_arm_cmsis_path");
    c.iar_arm_c_include = json_value(json, "iar_arm_c_include");
    c.iar_rl78_install_path = json_value(json, "iar_rl78_install_path");
    c.iar_rl78_c_include = json_value(json, "iar_rl78_c_include");
    c.iar_rl78_inc = json_value(json, "iar_rl78_inc");
    std::string keil = json_object(json, "keil");
    std::string iar = json_object(json, "iar");
    if (!keil.empty()) {
        if (c.keil_install_path.empty()) c.keil_install_path = json_value(keil, "install_path");
        if (c.keil_cmsis_path.empty()) c.keil_cmsis_path = json_value(keil, "cmsis_path");
        if (c.keil_armcc_include.empty()) c.keil_armcc_include = json_value(keil, "armcc_include");
        if (c.keil_armclang_include.empty()) c.keil_armclang_include = json_value(keil, "armclang_include");
    }
    if (!iar.empty()) {
        if (c.iar_install_path.empty()) c.iar_install_path = json_value(iar, "install_path");
        if (c.iar_cmsis_path.empty()) c.iar_cmsis_path = json_value(iar, "cmsis_path");
        if (c.iar_c_include.empty()) c.iar_c_include = json_value(iar, "c_include");
        if (c.iar_arm_install_path.empty()) c.iar_arm_install_path = json_value(iar, "arm_install_path");
        if (c.iar_arm_cmsis_path.empty()) c.iar_arm_cmsis_path = json_value(iar, "arm_cmsis_path");
        if (c.iar_arm_c_include.empty()) c.iar_arm_c_include = json_value(iar, "arm_c_include");
        if (c.iar_rl78_install_path.empty()) c.iar_rl78_install_path = json_value(iar, "rl78_install_path");
        if (c.iar_rl78_c_include.empty()) c.iar_rl78_c_include = json_value(iar, "rl78_c_include");
        if (c.iar_rl78_inc.empty()) c.iar_rl78_inc = json_value(iar, "rl78_inc");
    }
    if (c.iar_arm_install_path.empty()) c.iar_arm_install_path = c.iar_install_path;
    if (c.iar_arm_cmsis_path.empty()) c.iar_arm_cmsis_path = c.iar_cmsis_path;
    if (c.iar_arm_c_include.empty()) c.iar_arm_c_include = c.iar_c_include;
    return c;
}

static void save_config(const Config& c) {
    std::string arm_install = !c.iar_arm_install_path.empty() ? c.iar_arm_install_path : c.iar_install_path;
    std::string arm_cmsis = !c.iar_arm_cmsis_path.empty() ? c.iar_arm_cmsis_path : c.iar_cmsis_path;
    std::string arm_c_inc = !c.iar_arm_c_include.empty() ? c.iar_arm_c_include : c.iar_c_include;
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"version\": 2,\n";
    ss << "  \"keil\": {\n";
    ss << "    \"install_path\": \"" << json_escape_value(c.keil_install_path) << "\",\n";
    ss << "    \"cmsis_path\": \"" << json_escape_value(c.keil_cmsis_path) << "\",\n";
    ss << "    \"armcc_include\": \"" << json_escape_value(c.keil_armcc_include) << "\",\n";
    ss << "    \"armclang_include\": \"" << json_escape_value(c.keil_armclang_include) << "\"\n";
    ss << "  },\n";
    ss << "  \"iar\": {\n";
    ss << "    \"install_path\": \"" << json_escape_value(arm_install) << "\",\n";
    ss << "    \"cmsis_path\": \"" << json_escape_value(arm_cmsis) << "\",\n";
    ss << "    \"c_include\": \"" << json_escape_value(arm_c_inc) << "\",\n";
    ss << "    \"arm_install_path\": \"" << json_escape_value(arm_install) << "\",\n";
    ss << "    \"arm_cmsis_path\": \"" << json_escape_value(arm_cmsis) << "\",\n";
    ss << "    \"arm_c_include\": \"" << json_escape_value(arm_c_inc) << "\",\n";
    ss << "    \"rl78_install_path\": \"" << json_escape_value(c.iar_rl78_install_path) << "\",\n";
    ss << "    \"rl78_c_include\": \"" << json_escape_value(c.iar_rl78_c_include) << "\",\n";
    ss << "    \"rl78_inc\": \"" << json_escape_value(c.iar_rl78_inc) << "\"\n";
    ss << "  }\n";
    ss << "}\n";
    write_text(config_path(), ss.str());
}

static std::string path_if_dir(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p, ec) ? fs::weakly_canonical(p, ec).string() : "";
}

static std::string prompt_dir(const std::string& message) {
    std::cout << message;
    std::string manual;
    if (!std::getline(std::cin, manual)) return "";
    manual = trim(manual);
    if (!manual.empty() && manual.front() == '"') manual.erase(manual.begin());
    if (!manual.empty() && manual.back() == '"') manual.pop_back();
    return path_if_dir(manual);
}

static std::vector<fs::path> keil_cmsis_base_candidates(const fs::path& rte) {
    std::string leaf = lower_copy(rte.filename().string());
    if (leaf == "cmsis") return {rte};
    if (leaf == "packs" || leaf == "pack") return {rte / "ARM" / "CMSIS"};
    if (leaf == "arm") return {rte / "Packs" / "ARM" / "CMSIS", rte / "PACK" / "ARM" / "CMSIS", rte / "CMSIS"};
    return {rte / "ARM" / "Packs" / "ARM" / "CMSIS", rte / "ARM" / "PACK" / "ARM" / "CMSIS", rte / "Packs" / "ARM" / "CMSIS", rte / "PACK" / "ARM" / "CMSIS", rte / "ARM" / "CMSIS"};
}

struct ToolsIni {
    std::string armcc_include;
    std::string armclang_include;
    std::string rte_path;
};

static ToolsIni parse_tools_ini(const fs::path& keil_root) {
    ToolsIni out;
    fs::path ini = keil_root / "TOOLS.INI";
    if (!fs::exists(ini)) return out;
    std::string text = read_file(ini);
    std::string section;
    std::stringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        line = trim(line);
        if (line.empty()) continue;
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            std::transform(section.begin(), section.end(), section.begin(), [](unsigned char c){ return std::toupper(c); });
            continue;
        }
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string value = trim(line.substr(eq + 1));
        value = trim(value);
        if (!value.empty() && value.front() == '"') value.erase(value.begin());
        if (!value.empty() && value.back() == '"') value.pop_back();
        std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c){ return std::toupper(c); });
        fs::path p(value);
        if (!p.is_absolute()) p = keil_root / p;
        if (key == "RTEPATH" && out.rte_path.empty() && fs::is_directory(p)) out.rte_path = fs::weakly_canonical(p).string();
        if (key == "PATH") {
            if (section == "ARM") {
                if (out.armcc_include.empty()) out.armcc_include = path_if_dir(p / "ARMCC" / "include");
                if (out.armclang_include.empty()) out.armclang_include = path_if_dir(p / "ARMCLANG" / "include");
            } else if (section == "ARMCC") {
                out.armcc_include = path_if_dir(p / "include");
            } else if (section == "ARMCLANG") {
                out.armclang_include = path_if_dir(p / "include");
            }
        }
    }
    return out;
}

static std::vector<fs::path> keil_cmsis_bases(const fs::path& keil_root) {
    std::vector<fs::path> candidates;
    ToolsIni ini = parse_tools_ini(keil_root);
    if (!ini.rte_path.empty()) {
        auto rte_candidates = keil_cmsis_base_candidates(ini.rte_path);
        candidates.insert(candidates.end(), rte_candidates.begin(), rte_candidates.end());
    }
    fs::path arm = lower_copy(keil_root.filename().string()) == "arm" ? keil_root : keil_root / "ARM";
    candidates.push_back(arm / "Packs" / "ARM" / "CMSIS");
    candidates.push_back(arm / "PACK" / "ARM" / "CMSIS");
    candidates.push_back(arm / "CMSIS");

    std::vector<fs::path> out;
    std::set<std::string> seen;
    for (const auto& c : candidates) {
        std::error_code ec;
        std::string key = lower_copy(c.string());
        if (!seen.insert(key).second) continue;
        if (fs::is_directory(c, ec)) out.push_back(fs::weakly_canonical(c, ec));
    }
    return out;
}

static fs::path cmsis_base_from_keil(const fs::path& keil_root) {
    auto bases = keil_cmsis_bases(keil_root);
    if (!bases.empty()) return bases.front();
    return keil_root / "ARM" / "CMSIS";
}

static void append_cmsis_versions(std::vector<std::pair<std::string, std::string>>& out, const fs::path& base, bool multi_base) {
    std::error_code ec;
    if (!fs::is_directory(base, ec)) return;
    auto direct = base / "Core" / "Include";
    if (fs::is_directory(direct, ec)) {
        std::string label = multi_base ? base.parent_path().filename().string() + "/" + base.filename().string() + "/default" : "default";
        out.push_back({label, fs::weakly_canonical(direct, ec).string()});
    }
    auto legacy = base / "Include";
    if (fs::is_directory(legacy, ec)) {
        std::string label = multi_base ? base.parent_path().filename().string() + "/" + base.filename().string() + "/legacy" : "legacy";
        out.push_back({label, fs::weakly_canonical(legacy, ec).string()});
    }
    for (auto& e : fs::directory_iterator(base)) {
        if (!e.is_directory()) continue;
        std::vector<fs::path> candidates = {e.path() / "CMSIS" / "Core" / "Include", e.path() / "Core" / "Include"};
        for (auto& c : candidates) {
            if (fs::is_directory(c, ec)) {
                out.push_back({e.path().filename().string(), fs::weakly_canonical(c, ec).string()});
                break;
            }
        }
    }
}

static std::vector<std::pair<std::string, std::string>> cmsis_versions(const fs::path& base) {
    std::vector<std::pair<std::string, std::string>> out;
    append_cmsis_versions(out, base, false);
    std::sort(out.begin(), out.end());
    return out;
}

static std::vector<std::pair<std::string, std::string>> cmsis_versions_for_keil(const fs::path& keil_root) {
    std::vector<std::pair<std::string, std::string>> out;
    auto bases = keil_cmsis_bases(keil_root);
    bool multi_base = bases.size() > 1;
    for (const auto& base : bases) append_cmsis_versions(out, base, multi_base);
    std::sort(out.begin(), out.end());
    return out;
}

#ifdef _WIN32
static std::string wide_to_utf8(const std::wstring& ws) {
    if (ws.empty()) return "";
    int size = WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(size ? size - 1 : 0, '\0');
    if (size > 1) WideCharToMultiByte(CP_UTF8, 0, ws.c_str(), -1, s.data(), size, nullptr, nullptr);
    return s;
}

static std::string normalize_install_path(std::string raw, const std::string& tool);

static std::string read_reg_string(HKEY root, const std::wstring& subkey, const std::wstring& name, REGSAM view) {
    HKEY key{};
    if (RegOpenKeyExW(root, subkey.c_str(), 0, KEY_READ | view, &key) != ERROR_SUCCESS) return "";
    wchar_t buffer[1024];
    DWORD bytes = sizeof(buffer);
    DWORD type = 0;
    LONG ok = RegQueryValueExW(key, name.c_str(), nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &bytes);
    RegCloseKey(key);
    if (ok != ERROR_SUCCESS || (type != REG_SZ && type != REG_EXPAND_SZ)) return "";
    return wide_to_utf8(buffer);
}

static std::vector<std::string> enum_reg_keys(HKEY root, const std::wstring& subkey, REGSAM view) {
    std::vector<std::string> out;
    HKEY key{};
    if (RegOpenKeyExW(root, subkey.c_str(), 0, KEY_READ | view, &key) != ERROR_SUCCESS) return out;
    for (DWORD i = 0;; ++i) {
        wchar_t name[512];
        DWORD len = 512;
        if (RegEnumKeyExW(key, i, name, &len, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        out.push_back(wide_to_utf8(std::wstring(name, len)));
    }
    RegCloseKey(key);
    return out;
}

static void add_normalized_path(std::set<std::string>& found, const std::string& raw, const std::string& tool) {
    std::string p = normalize_install_path(raw, tool);
    if (!p.empty()) found.insert(p);
}

static std::string normalize_install_path(std::string raw, const std::string& tool) {
    if (raw.empty()) return "";
    fs::path p(trim(raw));
    std::error_code ec;
    if (!fs::is_directory(p, ec)) return "";
    p = fs::weakly_canonical(p, ec);
    std::string leaf = lower_copy(p.filename().string());
    if (tool == "keil" && leaf == "arm") p = p.parent_path();
    if (tool == "iar" && (leaf == "arm" || leaf == "rl78")) p = p.parent_path();
    return fs::weakly_canonical(p, ec).string();
}

static std::vector<std::string> scan_keil() {
    std::set<std::string> found;
    for (auto root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
        for (REGSAM view : {KEY_WOW64_32KEY, KEY_WOW64_64KEY}) {
            std::wstring base = L"SOFTWARE\\Keil\\Products";
            for (auto& k : enum_reg_keys(root, base, view)) {
                std::wstring wk(k.begin(), k.end());
                std::string p = normalize_install_path(read_reg_string(root, base + L"\\" + wk, L"PATH", view), "keil");
                if (!p.empty()) found.insert(p);
            }
        }
    }
    return {found.begin(), found.end()};
}

static std::vector<std::string> scan_iar() {
    std::set<std::string> found;
    for (auto root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
        for (REGSAM view : {KEY_WOW64_32KEY, KEY_WOW64_64KEY}) {
            for (const auto& base : {L"SOFTWARE\\IAR Systems\\Embedded Workbench", L"SOFTWARE\\WOW6432Node\\IAR Systems\\Embedded Workbench"}) {
                for (auto& k : enum_reg_keys(root, base, view)) {
                    std::wstring wk(k.begin(), k.end());
                    std::wstring key = std::wstring(base) + L"\\" + wk;
                    for (auto& value : {L"InstallPath", L"InstallLocation", L"Path"}) {
                        add_normalized_path(found, read_reg_string(root, key, value, view), "iar");
                    }
                    for (auto& child : enum_reg_keys(root, key, view)) {
                        std::wstring wc(child.begin(), child.end());
                        std::wstring child_key = key + L"\\" + wc;
                        for (auto& value : {L"InstallPath", L"InstallLocation", L"Path"}) {
                            add_normalized_path(found, read_reg_string(root, child_key, value, view), "iar");
                        }
                    }
                }
            }
            for (const auto& base : {L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall", L"SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"}) {
                for (auto& k : enum_reg_keys(root, base, view)) {
                    std::wstring wk(k.begin(), k.end());
                    std::wstring key = std::wstring(base) + L"\\" + wk;
                    std::string display = lower_copy(read_reg_string(root, key, L"DisplayName", view));
                    if (display.find("iar") == std::string::npos || display.find("embedded") == std::string::npos) continue;
                    for (auto& value : {L"InstallPath", L"InstallLocation"}) {
                        add_normalized_path(found, read_reg_string(root, key, value, view), "iar");
                    }
                }
            }
            for (const auto& base : {L"SOFTWARE\\IAR Systems", L"SOFTWARE\\WOW6432Node\\IAR Systems"}) {
                for (auto& value : {L"InstallPath", L"InstallLocation"}) {
                    add_normalized_path(found, read_reg_string(root, base, value, view), "iar");
                }
            }
        }
    }
    return {found.begin(), found.end()};
}
#else
static std::vector<std::string> scan_keil() { return {}; }
static std::vector<std::string> scan_iar() { return {}; }
#endif

static std::vector<std::string> scan_iar_by_arch(const std::string& arch) {
    std::vector<std::string> all = scan_iar();
    std::vector<std::string> out;
    for (const auto& s : all) {
        fs::path p(s);
        std::error_code ec;
        if (arch == "arm") {
            if (fs::is_directory(p / "arm", ec) || fs::is_directory(p / "arm" / "inc" / "c", ec)) {
                out.push_back(s);
            }
        } else if (arch == "rl78") {
            if (fs::is_directory(p / "rl78", ec) || fs::is_directory(p / "rl78" / "inc" / "c", ec)) {
                out.push_back(s);
            }
        }
    }
    return out;
}

static std::string choose_path(const std::string& title, const std::vector<std::string>& values) {
    std::cout << "\n" << title << "\n";
    if (values.empty()) {
        std::cout << "  No installation found.\nManual path (Enter to skip): ";
        std::string manual;
        if (!std::getline(std::cin, manual)) return "";
        manual = trim(manual);
        if (!manual.empty() && manual.front() == '"') manual.erase(manual.begin());
        if (!manual.empty() && manual.back() == '"') manual.pop_back();
        return fs::is_directory(manual) ? fs::weakly_canonical(manual).string() : "";
    }
    for (size_t i = 0; i < values.size(); ++i) std::cout << "  [" << (i + 1) << "] " << values[i] << "\n";
    std::cout << "  [0] Manual / skip\nSelect: ";
    std::string raw;
    if (!std::getline(std::cin, raw)) return "";
    int n = raw.empty() ? 0 : std::atoi(raw.c_str());
    if (n <= 0) {
        std::cout << "Manual path (Enter to skip): ";
        std::string manual;
        if (!std::getline(std::cin, manual)) return "";
        manual = trim(manual);
        if (!manual.empty() && manual.front() == '"') manual.erase(manual.begin());
        if (!manual.empty() && manual.back() == '"') manual.pop_back();
        return fs::is_directory(manual) ? fs::weakly_canonical(manual).string() : "";
    }
    if (static_cast<size_t>(n) > values.size()) return "";
    return values[n - 1];
}

static std::string choose_cmsis_include(const fs::path& base) {
    auto versions = cmsis_versions(base);
    if (versions.empty()) {
        std::cout << "\nNo CMSIS versions found under: " << base.string() << "\n";
        std::cout << "Manual CMSIS include path (Enter to skip): ";
        std::string manual;
        if (!std::getline(std::cin, manual)) return "";
        manual = trim(manual);
        if (!manual.empty() && manual.front() == '"') manual.erase(manual.begin());
        if (!manual.empty() && manual.back() == '"') manual.pop_back();
        return fs::is_directory(manual) ? fs::weakly_canonical(manual).string() : "";
    }
    std::cout << "\nSelect CMSIS version:\n";
    for (size_t i = 0; i < versions.size(); ++i) std::cout << "  [" << (i + 1) << "] " << versions[i].first << ": " << versions[i].second << "\n";
    std::cout << "  [0] Manual / skip\nSelect: ";
    std::string raw;
    if (!std::getline(std::cin, raw)) return "";
    int n = raw.empty() ? 0 : std::atoi(raw.c_str());
    if (n <= 0) {
        std::cout << "Manual CMSIS include path (Enter to skip): ";
        std::string manual;
        if (!std::getline(std::cin, manual)) return "";
        manual = trim(manual);
        if (!manual.empty() && manual.front() == '"') manual.erase(manual.begin());
        if (!manual.empty() && manual.back() == '"') manual.pop_back();
        return fs::is_directory(manual) ? fs::weakly_canonical(manual).string() : "";
    }
    if (static_cast<size_t>(n) > versions.size()) return "";
    return versions[n - 1].second;
}

static std::string choose_cmsis_include_from_versions(const std::vector<std::pair<std::string, std::string>>& versions, const std::string& fallback_base) {
    if (versions.empty()) {
        std::cout << "\nNo CMSIS versions found under: " << fallback_base << "\n";
        return prompt_dir("Manual CMSIS include path (Enter to skip): ");
    }
    std::cout << "\nSelect CMSIS version:\n";
    for (size_t i = 0; i < versions.size(); ++i) std::cout << "  [" << (i + 1) << "] " << versions[i].first << ": " << versions[i].second << "\n";
    std::cout << "  [0] Manual / skip\nSelect: ";
    std::string raw;
    if (!std::getline(std::cin, raw)) return "";
    int n = raw.empty() ? 0 : std::atoi(raw.c_str());
    if (n <= 0) return prompt_dir("Manual CMSIS include path (Enter to skip): ");
    if (static_cast<size_t>(n) > versions.size()) return "";
    return versions[n - 1].second;
}

static void setup_config() {
    Config c = load_config();
    std::cout << "Keil2JsonCpp setup\nConfig file: " << config_path().string() << "\n";
    std::string keil = choose_path("Detected Keil installations:", scan_keil());
    if (!keil.empty()) {
        c.keil_install_path = keil;
        ToolsIni ini = parse_tools_ini(c.keil_install_path);
        c.keil_armcc_include = ini.armcc_include;
        c.keil_armclang_include = ini.armclang_include;
        c.keil_cmsis_path = choose_cmsis_include_from_versions(
            cmsis_versions_for_keil(c.keil_install_path),
            cmsis_base_from_keil(c.keil_install_path).string());
    } else {
        std::cout << "Keil configuration skipped.\n";
    }

    std::string iar_arm = choose_path("Detected IAR for ARM installations:", scan_iar_by_arch("arm"));
    if (!iar_arm.empty()) {
        c.iar_arm_install_path = iar_arm;
        c.iar_install_path = iar_arm;
        fs::path root(iar_arm);
        c.iar_arm_cmsis_path = choose_cmsis_include(root / "arm" / "CMSIS");
        c.iar_cmsis_path = c.iar_arm_cmsis_path;
        c.iar_arm_c_include = path_if_dir(root / "arm" / "inc" / "c");
        if (c.iar_arm_c_include.empty()) c.iar_arm_c_include = prompt_dir("Enter IAR ARM C include path (Enter to skip): ");
        c.iar_c_include = c.iar_arm_c_include;
    } else {
        std::cout << "IAR ARM configuration skipped.\n";
    }

    std::string iar_rl78 = choose_path("Detected IAR for RL78 installations:", scan_iar_by_arch("rl78"));
    if (!iar_rl78.empty()) {
        c.iar_rl78_install_path = iar_rl78;
        fs::path root(iar_rl78);
        c.iar_rl78_c_include = path_if_dir(root / "rl78" / "inc" / "c");
        if (c.iar_rl78_c_include.empty()) c.iar_rl78_c_include = prompt_dir("Enter IAR RL78 C include path (Enter to skip): ");
        c.iar_rl78_inc = path_if_dir(root / "rl78" / "inc");
        if (c.iar_rl78_inc.empty()) c.iar_rl78_inc = prompt_dir("Enter IAR RL78 inc path (Enter to skip): ");
    } else {
        std::cout << "IAR RL78 configuration skipped.\n";
    }

    save_config(c);
    std::cout << "Configuration saved: " << config_path().string() << "\n";
}

static std::vector<std::string> tags(const std::string& xml, const std::string& name) {
    std::vector<std::string> out;
    std::regex re("<" + name + R"(>\s*([\s\S]*?)\s*</)" + name + ">", std::regex::icase);
    for (auto it = std::sregex_iterator(xml.begin(), xml.end(), re); it != std::sregex_iterator(); ++it) {
        out.push_back(trim((*it)[1].str()));
    }
    return out;
}

static std::string first_tag_text(const std::string& xml, const std::string& name) {
    for (auto& value : tags(xml, name)) {
        value = trim(value);
        if (!value.empty()) return value;
    }
    return "";
}

static std::string keil_various_controls(const std::string& xml) {
    std::regex re(R"(<TargetArmAds>[\s\S]*?<Cads>[\s\S]*?<VariousControls>\s*([\s\S]*?)\s*</VariousControls>)", std::regex::icase);
    std::smatch m;
    return std::regex_search(xml, m, re) ? m[1].str() : "";
}

static std::string choose_from_list(const std::string& title, const std::vector<std::string>& labels) {
    std::cout << title << "\n";
    for (size_t i = 0; i < labels.size(); ++i) std::cout << "  [" << (i + 1) << "] " << labels[i] << "\n";
    std::cout << "  [0] Skip\nSelect: ";
    std::string raw;
    if (!std::getline(std::cin, raw)) return "";
    int n = raw.empty() ? 0 : std::atoi(raw.c_str());
    if (n <= 0 || static_cast<size_t>(n) > labels.size()) return "";
    return labels[n - 1];
}

static std::vector<std::pair<std::string, std::string>> keil_target_blocks(const std::string& xml) {
    std::vector<std::pair<std::string, std::string>> out;
    std::regex target_re(R"(<Target>\s*([\s\S]*?)\s*</Target>)", std::regex::icase);
    for (auto it = std::sregex_iterator(xml.begin(), xml.end(), target_re); it != std::sregex_iterator(); ++it) {
        std::string block = (*it)[1].str();
        std::string name = first_tag_text(block, "TargetName");
        if (!name.empty()) out.push_back({name, block});
    }
    return out;
}

static std::string select_keil_target_xml(const std::string& xml, const std::string& target_name, std::string& selected_name) {
    auto targets = keil_target_blocks(xml);
    if (targets.empty()) return xml;
    if (!target_name.empty()) {
        std::string requested = normalize_name(target_name);
        for (const auto& item : targets) {
            if (normalize_name(item.first) == requested) {
                selected_name = item.first;
                return item.second;
            }
        }
        std::ostringstream available;
        for (size_t i = 0; i < targets.size(); ++i) {
            if (i) available << ", ";
            available << targets[i].first;
        }
        throw std::runtime_error("Keil target not found: " + target_name + ". Available targets: " + available.str());
    }
    if (targets.size() == 1) {
        selected_name = targets[0].first;
        return targets[0].second;
    }
    std::vector<std::string> labels;
    for (const auto& item : targets) labels.push_back(item.first);
    std::string selected = choose_from_list("Select Keil target for compile_commands.json:", labels);
    if (selected.empty()) {
        selected_name = targets[0].first;
        std::cout << "No target selected, defaulting to: " << selected_name << "\n";
        return targets[0].second;
    }
    for (const auto& item : targets) {
        if (item.first == selected) {
            selected_name = item.first;
            return item.second;
        }
    }
    selected_name = targets[0].first;
    return targets[0].second;
}

static fs::path resolve_path(const fs::path& root, std::string value) {
    value = trim(replace_all(value, "\\", "/"));
    if (value.empty()) return {};
    fs::path p(value);
    if (p.is_absolute()) return fs::weakly_canonical(p);
    return fs::weakly_canonical(root / p);
}

static std::string format_path(const fs::path& root, const fs::path& path, bool absolute) {
    fs::path p = fs::weakly_canonical(path);
    if (absolute) return slash(p);
    std::error_code ec;
    fs::path rel = fs::relative(p, root, ec);
    return ec ? slash(p) : slash(rel);
}

static std::string detect_keil_compiler_from_xml(const std::string& text) {
    std::string xml = lower_copy(text);
    std::regex uac6_re(R"(<uac6>\s*([0-9]+)\s*</uac6>)");
    std::smatch m;
    if (std::regex_search(xml, m, uac6_re) && std::atoi(m[1].str().c_str()) > 0) return "armclang";
    if (xml.find("armclang") != std::string::npos || xml.find("ac6") != std::string::npos) return "armclang";
    return "armcc";
}

static ProjectData parse_uvprojx(const fs::path& file, const fs::path& root, const Config& config, const std::string& target_name) {
    std::string xml = read_file(file);
    std::string selected_name;
    std::string parse_xml = select_keil_target_xml(xml, target_name, selected_name);
    if (!selected_name.empty()) std::cout << "Using Keil target: " << selected_name << "\n";
    ProjectData data;
    std::string controls = keil_various_controls(parse_xml);
    std::string include_text = first_tag_text(controls.empty() ? parse_xml : controls, "IncludePath");
    if (!include_text.empty()) {
        for (auto& item : split(include_text, ';')) data.includes.push_back(slash(resolve_path(root, item)));
    }
    std::string define_text = first_tag_text(controls.empty() ? parse_xml : controls, "Define");
    if (!define_text.empty()) {
        for (auto& item : split(define_text, ',')) data.defines.push_back(item);
    }
    for (auto& f : tags(parse_xml, "FilePath")) data.sources.push_back(slash(resolve_path(root, f)));
    if (!config.keil_cmsis_path.empty()) data.includes.push_back(config.keil_cmsis_path);
    std::string compiler = detect_keil_compiler_from_xml(parse_xml);
    if (compiler == "armclang" && !config.keil_armclang_include.empty()) data.includes.push_back(config.keil_armclang_include);
    if (compiler == "armcc" && !config.keil_armcc_include.empty()) data.includes.push_back(config.keil_armcc_include);
    data.includes = unique(data.includes);
    data.defines = unique(data.defines);
    data.sources = unique(data.sources);
    return data;
}

static void parse_eww(const fs::path& eww_file, std::vector<fs::path>& existing, std::vector<fs::path>& missing) {
    std::string xml = read_file(eww_file);
    fs::path ws_dir = eww_file.parent_path();
    std::regex re(R"(<project>\s*<path>\s*([^<]+)\s*</path>)", std::regex::icase);
    for (auto it = std::sregex_iterator(xml.begin(), xml.end(), re); it != std::sregex_iterator(); ++it) {
        std::string raw = trim((*it)[1].str());
        raw = replace_all(raw, "$WS_DIR$", ws_dir.string());
        fs::path p(raw);
        if (!p.is_absolute()) p = ws_dir / p;
        std::error_code ec;
        p = fs::weakly_canonical(p, ec);
        if (fs::exists(p, ec) && fs::is_regular_file(p, ec)) {
            existing.push_back(p);
        } else {
            missing.push_back(p);
        }
    }
}

static std::vector<std::string> parse_ewp_configurations(const fs::path& project) {
    std::string xml = read_file(project);
    std::vector<std::string> configs;
    std::regex re(R"(<configuration>\s*<name>\s*([^<]+)\s*</name>)", std::regex::icase);
    for (auto it = std::sregex_iterator(xml.begin(), xml.end(), re); it != std::sregex_iterator(); ++it) {
        std::string name = trim((*it)[1].str());
        if (!name.empty()) configs.push_back(name);
    }
    return unique(configs);
}

static std::string parse_ewp_toolchain(const fs::path& project) {
    std::string xml = read_file(project);
    std::regex re(R"(<toolchain>\s*<name>\s*([^<]+)\s*</name>)", std::regex::icase);
    std::smatch m;
    if (std::regex_search(xml, m, re)) {
        return trim(m[1].str());
    }
    return "ARM";
}

static ProjectData parse_ewp(const fs::path& file, const fs::path& root, const Config& config, const std::string& target_config = "") {
    std::string xml = read_file(file);
    fs::path ewp_dir = file.parent_path();
    ProjectData data;

    std::string chosen_cfg = target_config;
    if (chosen_cfg.empty()) {
        auto cfgs = parse_ewp_configurations(file);
        for (const auto& c : cfgs) {
            if (normalize_name(c) == "debug") {
                chosen_cfg = c;
                break;
            }
        }
        if (chosen_cfg.empty() && !cfgs.empty()) chosen_cfg = cfgs[0];
    }

    std::string parse_xml = xml;
    std::regex cfg_re(R"(<configuration>\s*([\s\S]*?)\s*</configuration>)", std::regex::icase);
    if (!chosen_cfg.empty()) {
        std::cout << "Using IAR configuration: " << chosen_cfg << "\n";
        for (auto it = std::sregex_iterator(xml.begin(), xml.end(), cfg_re); it != std::sregex_iterator(); ++it) {
            std::string block = (*it)[1].str();
            std::string name = first_tag_text(block, "name");
            if (normalize_name(name) == normalize_name(chosen_cfg)) {
                parse_xml = block;
                break;
            }
        }
    }

    std::regex tc_re(R"(<toolchain>\s*<name>\s*([^<]+)\s*</name>)", std::regex::icase);
    std::smatch m;
    if (std::regex_search(parse_xml, m, tc_re) || std::regex_search(xml, m, tc_re)) {
        data.toolchain = trim(m[1].str());
    } else {
        data.toolchain = "ARM";
    }

    std::regex option_re(R"(<option>\s*<name>\s*([^<]+)\s*</name>([\s\S]*?)</option>)", std::regex::icase);
    std::regex state_re(R"(<state>\s*([^<]+)\s*</state>)", std::regex::icase);
    for (auto it = std::sregex_iterator(parse_xml.begin(), parse_xml.end(), option_re); it != std::sregex_iterator(); ++it) {
        std::string name = trim((*it)[1].str());
        std::string body = (*it)[2].str();
        for (auto sit = std::sregex_iterator(body.begin(), body.end(), state_re); sit != std::sregex_iterator(); ++sit) {
            std::string value = trim((*sit)[1].str());
            value = replace_all(value, "$PROJ_DIR$", ewp_dir.string());
            if (name == "CCIncludePath2" || name == "CCIncludePath") data.includes.push_back(slash(resolve_path(root, value)));
            if (name == "CCDefines" || name == "CCDefines2") data.defines.push_back(value);
        }
    }
    std::regex file_re(R"(<file>\s*<name>\s*([^<]+)\s*</name>)", std::regex::icase);
    for (auto it = std::sregex_iterator(xml.begin(), xml.end(), file_re); it != std::sregex_iterator(); ++it) {
        std::string name = trim((*it)[1].str());
        std::string ext = lower_copy(fs::path(name).extension().string());
        if (ext == ".c" || ext == ".cpp" || ext == ".cc" || ext == ".cxx" || ext == ".s") {
            name = replace_all(name, "$PROJ_DIR$", ewp_dir.string());
            data.sources.push_back(slash(resolve_path(root, name)));
        }
    }

    if (lower_copy(data.toolchain) == "rl78") {
        std::string rl78_c_inc = config.iar_rl78_c_include;
        std::string rl78_inc = config.iar_rl78_inc;
        if (rl78_c_inc.empty() && !config.iar_rl78_install_path.empty()) {
            rl78_c_inc = path_if_dir(fs::path(config.iar_rl78_install_path) / "rl78" / "inc" / "c");
        }
        if (rl78_inc.empty() && !config.iar_rl78_install_path.empty()) {
            rl78_inc = path_if_dir(fs::path(config.iar_rl78_install_path) / "rl78" / "inc");
        }
        if (!rl78_c_inc.empty()) data.includes.push_back(rl78_c_inc);
        if (!rl78_inc.empty()) data.includes.push_back(rl78_inc);
    } else {
        std::string cmsis = !config.iar_arm_cmsis_path.empty() ? config.iar_arm_cmsis_path : config.iar_cmsis_path;
        std::string c_inc = !config.iar_arm_c_include.empty() ? config.iar_arm_c_include : config.iar_c_include;
        if (cmsis.empty() && !config.iar_arm_install_path.empty()) {
            cmsis = choose_cmsis_include(fs::path(config.iar_arm_install_path) / "arm" / "CMSIS");
        }
        if (c_inc.empty() && !config.iar_arm_install_path.empty()) {
            c_inc = path_if_dir(fs::path(config.iar_arm_install_path) / "arm" / "inc" / "c");
        }
        if (!cmsis.empty()) data.includes.push_back(cmsis);
        if (!c_inc.empty()) data.includes.push_back(c_inc);
    }

    data.includes = unique(data.includes);
    data.defines = unique(data.defines);
    data.sources = unique(data.sources);
    return data;
}

static std::string run_capture(const fs::path& cwd, const std::string& command) {
#ifdef _WIN32
    std::string cmd = "cd /d \"" + cwd.string() + "\" && " + command + " 2>&1";
#else
    std::string cmd = "cd \"" + cwd.string() + "\" && " + command + " 2>&1";
#endif
    std::array<char, 4096> buffer{};
    std::string result;
#ifdef _WIN32
    FILE* pipe = _popen(cmd.c_str(), "r");
#else
    FILE* pipe = popen(cmd.c_str(), "r");
#endif
    if (!pipe) return result;
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) result += buffer.data();
#ifdef _WIN32
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    return result;
}

static std::vector<std::string> shell_split(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool quote = false;
    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (c == '"') { quote = !quote; continue; }
        if (!quote && std::isspace(static_cast<unsigned char>(c))) {
            if (!cur.empty()) { out.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

static bool ends_with_any(const std::string& s, const std::vector<std::string>& suffixes) {
    std::string lower = s;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    for (auto& suffix : suffixes) {
        if (lower.size() >= suffix.size() && lower.substr(lower.size() - suffix.size()) == suffix) return true;
    }
    return false;
}

static std::vector<Entry> parse_makefile(const fs::path& root, bool dry_run) {
    std::cout << "Running: make clean\n";
    run_capture(root, "make clean");
    std::cout << "Running: make -n\n";
    std::string output = run_capture(root, "make -n");
    if (!dry_run) {
        std::cout << "Running: make\n";
        run_capture(root, "make");
    }

    std::vector<Entry> entries;
    std::stringstream ss(output);
    std::string line;
    while (std::getline(ss, line)) {
        auto tokens = shell_split(trim(line));
        if (tokens.empty()) continue;
        std::string compiler = tokens[0];
        if (!compiler.empty() && compiler[0] == '@') compiler.erase(compiler.begin());
        std::string exe = fs::path(compiler).filename().string();
        if (!ends_with_any(exe, {"gcc", "g++", "clang", "clang++"})) continue;
        if (std::find(tokens.begin(), tokens.end(), "-c") == tokens.end()) continue;
        Entry e;
        e.compiler = compiler;
        bool skip = false;
        for (size_t i = 1; i < tokens.size(); ++i) {
            std::string t = tokens[i];
            if (skip) { skip = false; continue; }
            if (t == "-o") { skip = true; continue; }
            if (t.rfind("-o", 0) == 0 && t != "-o") continue;
            if (t.rfind("-M", 0) == 0) {
                if (t == "-MF" || t == "-MT" || t == "-MQ") skip = true;
                continue;
            }
            if (ends_with_any(t, {".c", ".cc", ".cpp", ".cxx", ".s"})) {
                fs::path p(t);
                if (!p.is_absolute()) p = root / p;
                e.file = slash(fs::weakly_canonical(p));
                e.args.push_back(t);
                continue;
            }
            e.args.push_back(t);
        }
        if (!e.file.empty()) entries.push_back(e);
    }
    return entries;
}

static std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '"') out += "\\\"";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

static std::string quote_arg(const std::string& s) {
    if (s.find_first_of(" \t\"") == std::string::npos) return s;
    return "\"" + replace_all(s, "\"", "\\\"") + "\"";
}

static void write_json(const fs::path& output, const fs::path& root, const std::vector<Entry>& entries, bool absolute) {
    std::ofstream out(output);
    out << "[\n";
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        std::vector<std::string> args;
        args.push_back(e.compiler);
        for (auto t : e.args) {
            if (ends_with_any(t, {".c", ".cc", ".cpp", ".cxx", ".s"})) t = format_path(root, e.file, absolute);
            args.push_back(t);
        }
        std::string command;
        for (size_t j = 0; j < args.size(); ++j) {
            if (j) command += " ";
            command += quote_arg(args[j]);
        }
        out << "  {\n";
        out << "    \"command\": \"" << json_escape(command) << "\",\n";
        out << "    \"arguments\": [";
        for (size_t j = 0; j < args.size(); ++j) {
            if (j) out << ", ";
            out << "\"" << json_escape(args[j]) << "\"";
        }
        out << "],\n";
        out << "    \"directory\": \"" << json_escape(slash(root)) << "\",\n";
        out << "    \"file\": \"" << json_escape(format_path(root, e.file, absolute)) << "\"\n";
        out << "  }" << (i + 1 == entries.size() ? "\n" : ",\n");
    }
    out << "]\n";
}

static std::vector<Entry> from_project_data(const fs::path& root, const ProjectData& data, bool absolute) {
    std::vector<Entry> entries;
    std::string compiler = "arm-none-eabi-gcc";
    std::vector<std::string> base = {"-D__GNUC__"};
    if (lower_copy(data.toolchain) == "rl78") {
        compiler = "rl78-elf-gcc";
        base = {
            "-D__GNUC__",
            "-D__ICCRL78__=1",
            "-D__near=",
            "-D__far=",
            "-D__saddr=",
            "-D__sfr=",
            "-D__callt=",
            "-D__interrupt=",
            "-D__root=",
            "-D__no_init="
        };
    }
    for (auto& inc : data.includes) base.push_back("-I" + format_path(root, inc, absolute));
    for (auto& def : data.defines) base.push_back("-D" + def);
    for (auto& src : data.sources) {
        Entry e;
        e.compiler = compiler;
        e.file = src;
        e.args.push_back("-c");
        e.args.push_back(format_path(root, src, absolute));
        e.args.insert(e.args.end(), base.begin(), base.end());
        entries.push_back(e);
    }
    return entries;
}

static std::vector<std::string> parse_keil_targets(const fs::path& project) {
    std::vector<std::string> targets;
    for (auto& value : tags(read_file(project), "TargetName")) {
        value = trim(value);
        if (!value.empty()) targets.push_back(value);
    }
    return unique(targets);
}

struct ProjectSet {
    std::vector<fs::path> keil;
    std::vector<fs::path> iar;
    std::vector<fs::path> missing_iar;
    std::vector<fs::path> makefile_roots;
};

struct ProjectChoice {
    std::string kind;
    fs::path project;
    fs::path root;
    std::string selected_config;
};

static bool has_direct_makefile(const fs::path& root) {
    return fs::exists(root / "Makefile") || fs::exists(root / "makefile");
}

static ProjectSet collect_projects(fs::path input) {
    input = fs::weakly_canonical(input);
    ProjectSet projects;
    if (fs::is_regular_file(input)) {
        std::string ext = lower_copy(input.extension().string());
        std::string name = lower_copy(input.filename().string());
        if (ext == ".uvprojx") projects.keil.push_back(input);
        else if (ext == ".ewp") projects.iar.push_back(input);
        else if (ext == ".eww") parse_eww(input, projects.iar, projects.missing_iar);
        else if (name == "makefile") projects.makefile_roots.push_back(input.parent_path());
        return projects;
    }

    std::set<std::string> seen_iar;
    std::error_code ec;
    for (auto& p : fs::recursive_directory_iterator(input, ec)) {
        std::string ext = lower_copy(p.path().extension().string());
        if (ext == ".eww") {
            std::vector<fs::path> ex, mis;
            parse_eww(p.path(), ex, mis);
            for (const auto& m : mis) projects.missing_iar.push_back(m);
            for (const auto& e : ex) {
                if (seen_iar.insert(lower_copy(e.string())).second) {
                    projects.iar.push_back(e);
                }
            }
        }
    }
    for (auto& p : fs::recursive_directory_iterator(input, ec)) {
        std::string ext = lower_copy(p.path().extension().string());
        if (ext == ".uvprojx") projects.keil.push_back(p.path());
        else if (ext == ".ewp") {
            if (seen_iar.insert(lower_copy(p.path().string())).second) {
                projects.iar.push_back(p.path());
            }
        }
    }
    std::sort(projects.keil.begin(), projects.keil.end());
    std::sort(projects.iar.begin(), projects.iar.end());
    if (has_direct_makefile(input)) projects.makefile_roots.push_back(input);
    return projects;
}

static fs::path choose_project_file(const std::string& kind, std::vector<fs::path> files, const std::string& target) {
    if (kind == "keil" && !target.empty()) {
        std::vector<fs::path> matched;
        std::string requested = normalize_name(target);
        for (const auto& path : files) {
            for (const auto& item : parse_keil_targets(path)) {
                if (normalize_name(item) == requested) {
                    matched.push_back(path);
                    break;
                }
            }
        }
        if (matched.size() == 1) return matched[0];
        if (!matched.empty()) files = matched;
    }
    if (files.size() == 1) return files[0];
    std::vector<std::string> labels;
    for (const auto& path : files) labels.push_back(path.string());
    std::string selected = choose_from_list("Select " + kind + " project file:", labels);
    if (selected.empty()) {
        std::cout << "No project file selected, defaulting to: " << files[0].string() << "\n";
        return files[0];
    }
    return fs::path(selected);
}

static std::pair<fs::path, std::string> choose_iar_project_and_config(std::vector<fs::path> files, const std::string& target) {
    std::string target_ewp;
    std::string target_cfg;
    if (!target.empty()) {
        auto colon = target.find_first_of(":/");
        if (colon != std::string::npos) {
            target_ewp = target.substr(0, colon);
            target_cfg = target.substr(colon + 1);
        } else {
            std::string req = normalize_name(target);
            for (const auto& f : files) {
                if (normalize_name(f.stem().string()) == req) {
                    target_ewp = f.stem().string();
                    break;
                }
            }
            if (target_ewp.empty()) {
                for (const auto& f : files) {
                    auto cfgs = parse_ewp_configurations(f);
                    for (const auto& c : cfgs) {
                        if (normalize_name(c) == req) {
                            target_cfg = c;
                            target_ewp = f.stem().string();
                            break;
                        }
                    }
                    if (!target_ewp.empty()) break;
                }
            }
        }
    }

    fs::path chosen_file;
    if (!target_ewp.empty()) {
        std::string req = normalize_name(target_ewp);
        for (const auto& f : files) {
            if (normalize_name(f.stem().string()) == req) {
                chosen_file = f;
                break;
            }
        }
        if (chosen_file.empty()) {
            throw std::runtime_error("IAR project not found: " + target_ewp);
        }
    } else if (files.size() == 1) {
        chosen_file = files[0];
    } else {
        std::vector<std::string> labels;
        for (const auto& p : files) labels.push_back(p.string());
        std::string selected = choose_from_list("Select IAR project file:", labels);
        chosen_file = selected.empty() ? files[0] : fs::path(selected);
    }

    auto configs = parse_ewp_configurations(chosen_file);
    std::string chosen_config;
    if (!target_cfg.empty()) {
        std::string req = normalize_name(target_cfg);
        for (const auto& c : configs) {
            if (normalize_name(c) == req) {
                chosen_config = c;
                break;
            }
        }
        if (chosen_config.empty()) {
            throw std::runtime_error("IAR configuration not found: " + target_cfg);
        }
    } else if (configs.size() == 1) {
        chosen_config = configs[0];
    } else if (!configs.empty()) {
        std::string def_cfg = configs[0];
        for (const auto& c : configs) {
            if (normalize_name(c) == "debug") {
                def_cfg = c;
                break;
            }
        }
        bool is_interactive = false;
#ifdef _WIN32
        is_interactive = (_isatty(_fileno(stdin)) != 0);
#else
        is_interactive = (isatty(fileno(stdin)) != 0);
#endif
        if (is_interactive) {
            std::string sel = choose_from_list("Select IAR configuration for " + chosen_file.filename().string() + ":", configs);
            chosen_config = sel.empty() ? def_cfg : sel;
        } else {
            chosen_config = def_cfg;
        }
    }
    return {chosen_file, chosen_config};
}

static ProjectChoice detect_project(fs::path input, std::string project_type, const std::string& target) {
    ProjectSet projects = collect_projects(input);
    if (project_type == "make") project_type = "makefile";

    std::vector<std::pair<std::string, size_t>> available;
    if (!projects.keil.empty()) available.push_back({"keil", projects.keil.size()});
    if (!projects.iar.empty()) available.push_back({"iar", projects.iar.size()});
    if (!projects.makefile_roots.empty()) available.push_back({"makefile", projects.makefile_roots.size()});
    if (available.empty()) throw std::runtime_error("cannot find .uvprojx, .ewp, .eww, Makefile, or makefile");

    std::string kind;
    if (!project_type.empty()) {
        kind = project_type;
        if (kind != "keil" && kind != "iar" && kind != "makefile") throw std::runtime_error("unsupported project type: " + project_type);
        if ((kind == "keil" && projects.keil.empty()) ||
            (kind == "iar" && projects.iar.empty()) ||
            (kind == "makefile" && projects.makefile_roots.empty())) {
            throw std::runtime_error("cannot find project type: " + project_type);
        }
    } else if (available.size() == 1) {
        kind = available[0].first;
    } else {
        std::vector<std::string> labels;
        for (const auto& item : available) {
            const std::string& item_kind = item.first;
            fs::path first = item_kind == "keil" ? projects.keil[0] : (item_kind == "iar" ? projects.iar[0] : projects.makefile_roots[0]);
            std::string label = item_kind + ": " + first.string();
            if (item.second > 1) label += " (" + std::to_string(item.second) + " files)";
            labels.push_back(label);
        }
        std::string selected = choose_from_list("Multiple project types found, select generator branch:", labels);
        if (selected.empty()) {
            kind = available[0].first;
            std::cout << "No project type selected, defaulting to: " << kind << "\n";
        } else {
            auto colon = selected.find(':');
            kind = colon == std::string::npos ? selected : selected.substr(0, colon);
        }
    }

    if (kind == "makefile") {
        fs::path root = projects.makefile_roots[0];
        return {kind, root / "Makefile", root, ""};
    }
    if (kind == "keil") {
        fs::path project = choose_project_file(kind, projects.keil, target);
        fs::path root = fs::is_directory(input) ? input : project.parent_path();
        return {kind, project, root, ""};
    }
    auto [project, chosen_config] = choose_iar_project_and_config(projects.iar, target);
    fs::path root = fs::is_directory(input) ? input : project.parent_path();
    return {kind, project, root, chosen_config};
}

static int list_all_targets(const fs::path& input, const std::string& project_type) {
    ProjectSet projects = collect_projects(input);
    if (!projects.keil.empty() && (project_type.empty() || project_type == "keil")) {
        std::cout << "=== Keil MDK Projects ===\n";
        for (const auto& prj : projects.keil) {
            std::cout << "Project: " << prj.string() << "\n";
            auto targets = parse_keil_targets(prj);
            if (targets.empty()) {
                std::cout << "  No TargetName found.\n";
            } else {
                std::cout << "  Targets:\n";
                for (const auto& t : targets) std::cout << "    " << t << "\n";
            }
        }
    }
    if (!projects.iar.empty() && (project_type.empty() || project_type == "iar")) {
        std::cout << "=== IAR Projects ===\n";
        for (const auto& prj : projects.iar) {
            std::cout << "Project: " << prj.stem().string() << " (" << prj.string() << ")\n";
            std::cout << "  Toolchain: " << parse_ewp_toolchain(prj) << "\n";
            auto cfgs = parse_ewp_configurations(prj);
            if (cfgs.empty()) {
                std::cout << "  No configurations found.\n";
            } else {
                std::cout << "  Configurations:\n";
                for (const auto& c : cfgs) std::cout << "    " << c << "\n";
            }
        }
    }
    for (const auto& m : projects.missing_iar) {
        std::cout << "Warning: Referenced project does not exist: " << m.string() << "\n";
    }
    if (projects.keil.empty() && projects.iar.empty()) {
        if (!projects.makefile_roots.empty()) {
            std::cout << "=== Makefile Project ===\nPath: " << projects.makefile_roots[0].string() << "\n";
        } else {
            std::cout << "No Keil or IAR projects found.\n";
        }
    }
    return 0;
}

static std::string find_uv4_executable(const Config& config, const std::string& override_path) {
    std::vector<fs::path> candidates;
    if (!override_path.empty()) candidates.push_back(override_path);
    if (!config.keil_install_path.empty()) {
        fs::path root(config.keil_install_path);
        candidates.push_back(root / "UV4" / "UV4.exe");
        if (lower_copy(root.filename().string()) == "arm") candidates.push_back(root.parent_path() / "UV4" / "UV4.exe");
    }
#ifdef _WIN32
    candidates.push_back("C:\\Keil_v5\\UV4\\UV4.exe");
    candidates.push_back("C:\\Keil\\UV4\\UV4.exe");
    const char* path_env = std::getenv("PATH");
    if (path_env) {
        for (auto& dir : split(path_env, ';')) candidates.push_back(fs::path(dir) / "UV4.exe");
    }
#endif
    std::set<std::string> seen;
    for (const auto& candidate : candidates) {
        std::error_code ec;
        std::string key = lower_copy(candidate.string());
        if (!seen.insert(key).second) continue;
        if (fs::is_regular_file(candidate, ec)) return fs::weakly_canonical(candidate, ec).string();
    }
    return "";
}

static std::vector<std::string> build_keil_uv4_command(
    const std::string& uv4,
    const fs::path& project,
    const fs::path& output,
    const std::string& action,
    const std::string& target,
    int jobs,
    bool has_jobs,
    bool show_window) {
    std::string flag;
    if (action == "build") flag = "-b";
    else if (action == "rebuild") flag = "-r";
    else if (action == "clean") flag = "-c";
    else if (action == "flash" || action == "download") flag = "-f";
    else if (action == "debug") flag = "-d";
    else throw std::runtime_error("unsupported Keil action: " + action);

    std::vector<std::string> command{uv4};
    if (action != "debug" && !show_window) {
        if (!has_jobs) jobs = (action == "build") ? 16 : 0;
        command.push_back("-j" + std::to_string(jobs));
    }
    command.push_back(flag);
    command.push_back(project.string());
    command.push_back("-o");
    command.push_back(output.string());
    if (!target.empty()) {
        command.push_back("-t");
        command.push_back(target);
    }
    return command;
}

static std::string command_line(const std::vector<std::string>& args) {
    std::string line;
    for (size_t i = 0; i < args.size(); ++i) {
        if (i) line += " ";
        line += quote_arg(args[i]);
    }
    return line;
}

static std::uintmax_t print_new_log_content(const fs::path& log_file, std::uintmax_t position) {
    std::error_code ec;
    if (!fs::exists(log_file, ec)) return position;
    std::uintmax_t size = fs::file_size(log_file, ec);
    if (ec || size <= position) return position;

    std::ifstream in(log_file, std::ios::binary);
    if (!in) return position;
    in.seekg(static_cast<std::streamoff>(position), std::ios::beg);
    std::string data(static_cast<size_t>(size - position), '\0');
    in.read(data.data(), static_cast<std::streamsize>(data.size()));
    data.resize(static_cast<size_t>(in.gcount()));
    if (!data.empty()) {
        std::cout << replace_all(data, "\\", "/") << std::flush;
    }
    return position + data.size();
}

static int run_keil_uv4(
    const fs::path& input,
    const Config& config,
    const std::string& action,
    const std::string& target,
    int jobs,
    bool has_jobs,
    bool show_window,
    const std::string& uv4_override,
    const std::string& log_override,
    bool list_targets) {
#ifndef _WIN32
    (void)input; (void)config; (void)action; (void)target; (void)jobs; (void)has_jobs;
    (void)show_window; (void)uv4_override; (void)log_override; (void)list_targets;
    throw std::runtime_error("Keil UV4 command execution is only supported on Windows.");
#else
    ProjectChoice choice = detect_project(input, "keil", target);
    fs::path root = choice.root;
    fs::path project = choice.project;
    if (project.extension() != ".uvprojx") throw std::runtime_error("Keil UV4 requires a .uvprojx project: " + project.string());
    auto targets = parse_keil_targets(project);
    if (list_targets) {
        std::cout << "Project: " << project.string() << "\n";
        if (targets.empty()) {
            std::cout << "No TargetName found.\n";
        } else {
            std::cout << "Targets:\n";
            for (const auto& t : targets) std::cout << "  " << t << "\n";
        }
        return 0;
    }
    if (!target.empty() && !targets.empty() &&
        std::none_of(targets.begin(), targets.end(), [&](const std::string& item) { return normalize_name(item) == normalize_name(target); })) {
        std::cout << "Warning: target '" << target << "' was not found in project target list.\n";
        std::cout << "Available targets:\n";
        for (const auto& t : targets) std::cout << "  " << t << "\n";
    }
    std::string uv4 = find_uv4_executable(config, uv4_override);
    if (uv4.empty()) throw std::runtime_error("UV4.exe was not found. Run --setup or pass --keil_uv4.");
    fs::path output = log_override.empty() ? root / (action == "build" ? "build_log" : "Prg_Output") : fs::path(log_override);
    fs::create_directories(output.parent_path());
    std::ofstream(output, std::ios::trunc).close();
    auto command = build_keil_uv4_command(uv4, project, output, action, target, jobs, has_jobs, show_window);
    std::cout << "Running: " << command_line(command) << "\n";

    SHELLEXECUTEINFOW sei{};
    std::wstring exe(command[0].begin(), command[0].end());
    std::string params;
    for (size_t i = 1; i < command.size(); ++i) {
        if (i > 1) params += " ";
        params += quote_arg(command[i]);
    }
    std::wstring wparams(params.begin(), params.end());
    std::string cwd = root.string();
    std::wstring wcwd(cwd.begin(), cwd.end());
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS;
    sei.lpFile = exe.c_str();
    sei.lpParameters = wparams.c_str();
    sei.lpDirectory = wcwd.c_str();
    sei.nShow = show_window || action == "debug" ? SW_SHOWNORMAL : SW_HIDE;
    if (!ShellExecuteExW(&sei)) throw std::runtime_error("failed to start UV4.exe");

    std::uintmax_t log_position = 0;
    while (WaitForSingleObject(sei.hProcess, 100) == WAIT_TIMEOUT) {
        log_position = print_new_log_content(output, log_position);
    }
    log_position = print_new_log_content(output, log_position);

    DWORD code = 0;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    std::cout << "Keil " << action << " finished with exit code " << code << ".\n";
    return static_cast<int>(code);
#endif
}

int main(int argc, char** argv) {
    fs::path input = fs::current_path();
    bool absolute = false;
    bool setup = false;
    bool show_config = false;
    bool keil_build = false;
    bool list_targets = false;
    bool keil_window = false;
    bool dry_run = false;
    bool has_keil_jobs = false;
    int keil_jobs = 0;
    std::string keil_action = "build";
    std::string target;
    std::string project_type;
    std::string keil_uv4;
    std::string keil_log;
    bool path_specified = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if ((a == "-p" || a == "--path") && i + 1 < argc) {
            input = argv[++i];
            path_specified = true;
        }
        else if (a == "-a" || a == "--absolute") absolute = true;
        else if (a == "-s" || a == "--setup") setup = true;
        else if (a == "--show-config") show_config = true;
        else if ((a == "--project-type") && i + 1 < argc) project_type = argv[++i];
        else if (a == "-n" || a == "--dry-run") dry_run = true;
        else if (a == "--keil_build") keil_build = true;
        else if (a == "--list-targets") list_targets = true;
        else if (a == "--keil_window") keil_window = true;
        else if (a == "--keil_action" && i + 1 < argc) keil_action = argv[++i];
        else if ((a == "-t" || a == "--target") && i + 1 < argc) target = argv[++i];
        else if (a == "--keil_uv4" && i + 1 < argc) keil_uv4 = argv[++i];
        else if (a == "--keil_log" && i + 1 < argc) keil_log = argv[++i];
        else if (a == "--keil_jobs" && i + 1 < argc) {
            keil_jobs = std::atoi(argv[++i]);
            has_keil_jobs = true;
        }
        else if (!a.empty() && a[0] != '-') {
            if (!path_specified) {
                input = a;
                path_specified = true;
            }
        }
        else if (a == "-h" || a == "--help") {
            std::cout
                << "Usage: Keil2JsonCpp [-p path] [-a] [--setup] [--show-config]\n"
                << "       Keil2JsonCpp -p path [--project-type keil|iar|makefile|make] [-t target]\n"
                << "       Keil2JsonCpp -p path --list-targets\n"
                << "       Keil2JsonCpp -p path --keil_build [--keil_action build|rebuild|clean|flash|download|debug] [-t target]\n"
                << "\nOptions:\n"
                << "  -p, --path PATH       Project path or project file path\n"
                << "  -a, --absolute        Format compile_commands.json paths as absolute\n"
                << "  --project-type TYPE   Select generator branch: keil, iar, makefile, or make\n"
                << "  -s, --setup           Run setup wizard and save config\n"
                << "  --show-config         Print saved config and exit\n"
                << "  -n, --dry-run         For Makefile projects skip the final make after make clean and make -n\n"
                << "  --keil_build          Run Keil UV4 command instead of generating compile_commands.json\n"
                << "  --keil_action ACTION  build, rebuild, clean, flash, download, or debug\n"
                << "  -t, --target TARGET   Keil target name\n"
                << "  --list-targets        List Keil targets and exit\n"
                << "  --keil_uv4 PATH       Override UV4.exe path\n"
                << "  --keil_jobs N         Keil UV4 -j value when hiding Keil window; debug never uses -j\n"
                << "  --keil_log PATH       Keil UV4 output log path\n"
                << "  --keil_window         Show Keil window; debug always shows the window\n";
            return 0;
        }
    }
    input = fs::absolute(input);
    try {
        if (show_config) {
            std::cout << "Config file: " << config_path().string() << "\n";
            if (fs::exists(config_path())) std::cout << read_file(config_path());
            else std::cout << "{}\n";
            return 0;
        }
        if (setup || !fs::exists(config_path())) {
            setup_config();
            if (setup && argc <= 2 && !keil_build && !list_targets) return 0;
        }
        Config config = load_config();
        if (list_targets) {
            return list_all_targets(input, project_type);
        }
        if (keil_build) {
            return run_keil_uv4(
                input,
                config,
                keil_action,
                target,
                keil_jobs,
                has_keil_jobs,
                keil_window || keil_action == "debug",
                keil_uv4,
                keil_log,
                false);
        }
        ProjectChoice choice = detect_project(input, project_type, target);
        fs::path root = choice.root;
        fs::path project = choice.project;
        std::vector<Entry> entries;
        if (choice.kind == "keil") {
            std::cout << "Detected Keil project\n";
            entries = from_project_data(root, parse_uvprojx(project, root, config, target), absolute);
        } else if (choice.kind == "iar") {
            std::string tc = parse_ewp_toolchain(project);
            std::cout << "Detected IAR " << tc << " project\n";
            entries = from_project_data(root, parse_ewp(project, root, config, choice.selected_config), absolute);
        } else {
            std::cout << "Detected Makefile project\n";
            entries = parse_makefile(root, dry_run);
        }
        write_json(root / "compile_commands.json", root, entries, absolute);
        std::cout << "generate complete: " << (root / "compile_commands.json").string() << " (" << entries.size() << " files)\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
