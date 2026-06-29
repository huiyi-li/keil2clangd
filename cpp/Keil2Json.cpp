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
};

struct Entry {
    std::string compiler;
    std::string file;
    std::vector<std::string> args;
};

struct ProjectData {
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
    }
    return c;
}

static void save_config(const Config& c) {
    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"version\": 1,\n";
    ss << "  \"keil\": {\n";
    ss << "    \"install_path\": \"" << json_escape_value(c.keil_install_path) << "\",\n";
    ss << "    \"cmsis_path\": \"" << json_escape_value(c.keil_cmsis_path) << "\",\n";
    ss << "    \"armcc_include\": \"" << json_escape_value(c.keil_armcc_include) << "\",\n";
    ss << "    \"armclang_include\": \"" << json_escape_value(c.keil_armclang_include) << "\"\n";
    ss << "  },\n";
    ss << "  \"iar\": {\n";
    ss << "    \"install_path\": \"" << json_escape_value(c.iar_install_path) << "\",\n";
    ss << "    \"cmsis_path\": \"" << json_escape_value(c.iar_cmsis_path) << "\",\n";
    ss << "    \"c_include\": \"" << json_escape_value(c.iar_c_include) << "\"\n";
    ss << "  }\n";
    ss << "}\n";
    write_text(config_path(), ss.str());
}

static std::string path_if_dir(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p, ec) ? fs::weakly_canonical(p, ec).string() : "";
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

static fs::path cmsis_base_from_keil(const fs::path& keil_root) {
    ToolsIni ini = parse_tools_ini(keil_root);
    if (!ini.rte_path.empty()) {
        for (const auto& c : keil_cmsis_base_candidates(ini.rte_path)) {
            if (fs::is_directory(c)) return fs::weakly_canonical(c);
        }
    }
    return keil_root / "ARM" / "CMSIS";
}

static std::vector<std::pair<std::string, std::string>> cmsis_versions(const fs::path& base) {
    std::vector<std::pair<std::string, std::string>> out;
    if (!fs::is_directory(base)) return out;
    auto direct = base / "Core" / "Include";
    if (fs::is_directory(direct)) out.push_back({"default", fs::weakly_canonical(direct).string()});
    for (auto& e : fs::directory_iterator(base)) {
        if (!e.is_directory()) continue;
        std::vector<fs::path> candidates = {e.path() / "CMSIS" / "Core" / "Include", e.path() / "Core" / "Include"};
        for (auto& c : candidates) {
            if (fs::is_directory(c)) {
                out.push_back({e.path().filename().string(), fs::weakly_canonical(c).string()});
                break;
            }
        }
    }
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
    if ((tool == "keil" || tool == "iar") && leaf == "arm") p = p.parent_path();
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

static void setup_config() {
    Config c;
    std::cout << "Keil2JsonCpp setup\nConfig file: " << config_path().string() << "\n";
    c.keil_install_path = choose_path("Detected Keil installations:", scan_keil());
    if (!c.keil_install_path.empty()) {
        ToolsIni ini = parse_tools_ini(c.keil_install_path);
        c.keil_armcc_include = ini.armcc_include;
        c.keil_armclang_include = ini.armclang_include;
        c.keil_cmsis_path = choose_cmsis_include(cmsis_base_from_keil(c.keil_install_path));
    }
    c.iar_install_path = choose_path("Detected IAR installations:", scan_iar());
    if (!c.iar_install_path.empty()) {
        fs::path root(c.iar_install_path);
        c.iar_cmsis_path = choose_cmsis_include(root / "arm" / "CMSIS");
        c.iar_c_include = path_if_dir(root / "arm" / "inc" / "c");
    }
    save_config(c);
    std::cout << "Configuration saved.\n";
}

static std::vector<std::string> tags(const std::string& xml, const std::string& name) {
    std::vector<std::string> out;
    std::regex re("<" + name + R"(>\s*([\s\S]*?)\s*</)" + name + ">", std::regex::icase);
    for (auto it = std::sregex_iterator(xml.begin(), xml.end(), re); it != std::sregex_iterator(); ++it) {
        out.push_back(trim((*it)[1].str()));
    }
    return out;
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

static std::string detect_keil_compiler(const fs::path& file) {
    std::string xml = lower_copy(read_file(file));
    std::regex uac6_re(R"(<uac6>\s*([0-9]+)\s*</uac6>)");
    std::smatch m;
    if (std::regex_search(xml, m, uac6_re) && std::atoi(m[1].str().c_str()) > 0) return "armclang";
    if (xml.find("armclang") != std::string::npos || xml.find("ac6") != std::string::npos) return "armclang";
    return "armcc";
}

static ProjectData parse_uvprojx(const fs::path& file, const fs::path& root, const Config& config) {
    std::string xml = read_file(file);
    ProjectData data;
    auto include_tags = tags(xml, "IncludePath");
    if (!include_tags.empty()) {
        for (auto& item : split(include_tags.front(), ';')) data.includes.push_back(slash(resolve_path(root, item)));
    }
    auto define_tags = tags(xml, "Define");
    if (!define_tags.empty()) {
        for (auto& item : split(define_tags.front(), ',')) data.defines.push_back(item);
    }
    for (auto& f : tags(xml, "FilePath")) data.sources.push_back(slash(resolve_path(root, f)));
    if (!config.keil_cmsis_path.empty()) data.includes.push_back(config.keil_cmsis_path);
    std::string compiler = detect_keil_compiler(file);
    if (compiler == "armclang" && !config.keil_armclang_include.empty()) data.includes.push_back(config.keil_armclang_include);
    if (compiler == "armcc" && !config.keil_armcc_include.empty()) data.includes.push_back(config.keil_armcc_include);
    data.includes = unique(data.includes);
    data.defines = unique(data.defines);
    data.sources = unique(data.sources);
    return data;
}

static ProjectData parse_ewp(const fs::path& file, const fs::path& root, const Config& config) {
    std::string xml = read_file(file);
    ProjectData data;
    std::regex option_re(R"(<option>\s*<name>\s*([^<]+)\s*</name>([\s\S]*?)</option>)", std::regex::icase);
    std::regex state_re(R"(<state>\s*([^<]+)\s*</state>)", std::regex::icase);
    for (auto it = std::sregex_iterator(xml.begin(), xml.end(), option_re); it != std::sregex_iterator(); ++it) {
        std::string name = trim((*it)[1].str());
        std::string body = (*it)[2].str();
        for (auto sit = std::sregex_iterator(body.begin(), body.end(), state_re); sit != std::sregex_iterator(); ++sit) {
            std::string value = trim((*sit)[1].str());
            value = replace_all(value, "$PROJ_DIR$", ".");
            if (name == "CCIncludePath2" || name == "CCIncludePath") data.includes.push_back(slash(resolve_path(root, value)));
            if (name == "CCDefines" || name == "CCDefines2") data.defines.push_back(value);
        }
    }
    for (auto& name : tags(xml, "name")) {
        if (name.find(".c") == std::string::npos && name.find(".cpp") == std::string::npos && name.find(".s") == std::string::npos) continue;
        name = replace_all(name, "$PROJ_DIR$", ".");
        data.sources.push_back(slash(resolve_path(root, name)));
    }
    if (!config.iar_cmsis_path.empty()) data.includes.push_back(config.iar_cmsis_path);
    if (!config.iar_c_include.empty()) data.includes.push_back(config.iar_c_include);
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
    FILE* pipe = _popen(cmd.c_str(), "r");
    if (!pipe) return result;
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe)) result += buffer.data();
    _pclose(pipe);
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

static std::vector<Entry> parse_makefile(const fs::path& root) {
    std::cout << "Running: make clean\n";
    run_capture(root, "make clean");
    std::cout << "Running: make -n\n";
    std::string output = run_capture(root, "make -n");
    std::cout << "Running: make\n";
    run_capture(root, "make");

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
    std::vector<std::string> base = {"-D__GNUC__"};
    for (auto& inc : data.includes) base.push_back("-I" + format_path(root, inc, absolute));
    for (auto& def : data.defines) base.push_back("-D" + def);
    for (auto& src : data.sources) {
        Entry e;
        e.compiler = "arm-none-eabi-gcc";
        e.file = src;
        e.args.push_back("-c");
        e.args.push_back(format_path(root, src, absolute));
        e.args.insert(e.args.end(), base.begin(), base.end());
        entries.push_back(e);
    }
    return entries;
}

static fs::path find_project(fs::path input, fs::path& root) {
    input = fs::weakly_canonical(input);
    if (fs::is_regular_file(input)) {
        root = input.parent_path();
        return input;
    }
    root = input;
    for (auto& p : fs::recursive_directory_iterator(input)) {
        if (p.path().extension() == ".uvprojx" || p.path().extension() == ".ewp") {
            root = p.path().parent_path();
            return p.path();
        }
    }
    for (auto& name : {"Makefile", "makefile"}) {
        fs::path p = input / name;
        if (fs::exists(p)) return p;
    }
    throw std::runtime_error("cannot find .uvprojx, .ewp, Makefile, or makefile");
}

int main(int argc, char** argv) {
    fs::path input = fs::current_path();
    bool absolute = false;
    bool setup = false;
    bool show_config = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if ((a == "-p" || a == "--path") && i + 1 < argc) input = argv[++i];
        else if (a == "-a" || a == "--absolute") absolute = true;
        else if (a == "-s" || a == "--setup") setup = true;
        else if (a == "--show-config") show_config = true;
        else if (a == "-h" || a == "--help") {
            std::cout << "Usage: Keil2JsonCpp [-p path] [-a] [--setup] [--show-config]\n";
            return 0;
        }
    }
    try {
        if (show_config) {
            std::cout << "Config file: " << config_path().string() << "\n";
            if (fs::exists(config_path())) std::cout << read_file(config_path());
            else std::cout << "{}\n";
            return 0;
        }
        if (setup || !fs::exists(config_path())) {
            setup_config();
            if (setup && argc <= 2) return 0;
        }
        Config config = load_config();
        fs::path root;
        fs::path project = find_project(input, root);
        std::vector<Entry> entries;
        if (project.extension() == ".uvprojx") {
            std::cout << "Detected Keil project\n";
            entries = from_project_data(root, parse_uvprojx(project, root, config), absolute);
        } else if (project.extension() == ".ewp") {
            std::cout << "Detected IAR EWARM project\n";
            entries = from_project_data(root, parse_ewp(project, root, config), absolute);
        } else {
            std::cout << "Detected Makefile project\n";
            entries = parse_makefile(root);
        }
        write_json(root / "compile_commands.json", root, entries, absolute);
        std::cout << "generate complete: " << (root / "compile_commands.json").string() << " (" << entries.size() << " files)\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
