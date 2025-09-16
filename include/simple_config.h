#pragma once
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <stdexcept>

class SimpleConfig {
 public:
  bool loadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::string line;
    size_t lineNo = 0;
    while (std::getline(in, line)) {
      ++lineNo;
      trim(line);
      if (line.empty() || line[0] == '#') continue;
      auto eq = line.find('=');
      if (eq == std::string::npos) continue; // ignore malformed lines
      std::string key = line.substr(0, eq);
      std::string val = line.substr(eq + 1);
      trim(key);
      trim(val);
      if (!key.empty()) data_[key] = val;
    }
    return true;
  }

  bool has(const std::string& key) const {
    return data_.find(key) != data_.end();
  }

  std::string getString(const std::string& key, const std::string& def = "") const {
    auto it = data_.find(key);
    return it == data_.end() ? def : it->second;
  }

  double getDouble(const std::string& key, double def = 0.0) const {
    auto it = data_.find(key);
    if (it == data_.end()) return def;
    try { return std::stod(it->second); } catch (...) { return def; }
  }

  int getInt(const std::string& key, int def = 0) const {
    auto it = data_.find(key);
    if (it == data_.end()) return def;
    try { return std::stoi(it->second); } catch (...) { return def; }
  }

  long getLong(const std::string& key, long def = 0) const {
    auto it = data_.find(key);
    if (it == data_.end()) return def;
    try { return std::stol(it->second); } catch (...) { return def; }
  }

  // Required accessors: throw if missing or invalid
 std::string requireString(const std::string& key) const {
   auto it = data_.find(key);
   if (it == data_.end()) throw std::runtime_error("Missing required config key: " + key);
   return it->second;
 }
 double requireDouble(const std::string& key) const {
   auto it = data_.find(key);
   if (it == data_.end()) throw std::runtime_error("Missing required config key: " + key);
   try { return std::stod(it->second); } catch (...) {
     throw std::runtime_error(std::string("Invalid double for key: ") + key + ", value: " + it->second);
   }
 }
 int requireInt(const std::string& key) const {
   auto it = data_.find(key);
   if (it == data_.end()) throw std::runtime_error("Missing required config key: " + key);
   try { return std::stoi(it->second); } catch (...) {
     throw std::runtime_error(std::string("Invalid int for key: ") + key + ", value: " + it->second);
   }
 }
 long requireLong(const std::string& key) const {
   auto it = data_.find(key);
   if (it == data_.end()) throw std::runtime_error("Missing required config key: " + key);
   try { return std::stol(it->second); } catch (...) {
     throw std::runtime_error(std::string("Invalid long for key: ") + key + ", value: " + it->second);
   }
 }

private:
 static void trim(std::string& s) {
    const char* ws = " \t\r\n";
    auto b = s.find_first_not_of(ws);
    if (b == std::string::npos) { s.clear(); return; }
    auto e = s.find_last_not_of(ws);
    s = s.substr(b, e - b + 1);
  }

  std::unordered_map<std::string, std::string> data_;
};

namespace RovoConfig {
inline std::string resolveConfigPathFromArgs(int argc, char** argv, const std::string& defaultPath = "config.txt") {
  std::string path;
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      // caller can handle help; we just skip here
      continue;
    }
    if (arg.rfind("--config=", 0) == 0) {
      std::string val = arg.substr(std::string("--config=").size());
      path = val.empty() ? defaultPath : val;
      break;
    }
    if (arg == "--config") {
      if (i + 1 < argc) {
        std::string next = argv[i + 1];
        if (!next.empty() && next[0] != '-') {
          path = next;
        } else {
          path = defaultPath;
        }
      } else {
        path = defaultPath;
      }
      break;
    }
  }
  if (path.empty()) path = defaultPath;
  return path;
}
}
