#include "test_helpers.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>

// Trimming functions from https://stackoverflow.com/a/217605
static inline void
ltrim(std::string& s) {
  s.erase(s.begin(),
          std::find_if(s.begin(),
                       s.end(),
                       std::not1(std::ptr_fun<int, int>(std::isspace))));
}
static inline void
rtrim(std::string& s) {
  s.erase(std::find_if(s.rbegin(),
                       s.rend(),
                       std::not1(std::ptr_fun<int, int>(std::isspace)))
            .base(),
          s.end());
}
static inline void
trim(std::string& s) {
  ltrim(s);
  rtrim(s);
}

std::pair<std::string, int>
exec(const char* cmd) {
  std::array<char, 4096> buffer;
  std::string result;
  std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
  if(!pipe) {
    throw std::runtime_error("popen() failed!");
  }
  while(std::fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
    result += buffer.data();
  }
  int exited = pclose(pipe.release());
  int retcode = -1;
  if(WIFEXITED(exited)) {
    retcode = WEXITSTATUS(exited);
  }
  return { result, retcode };
}

std::unordered_map<std::string, std::string>
parse_runlim_prints(const std::string& str) {
  std::unordered_map<std::string, std::string> properties;

  std::istringstream in{ str };
  std::string line;
  while(std::getline(in, line)) {
    trim(line);
    auto beginpos = line.find("]") + 2;
    auto seppos = line.find(":");
    if(seppos != std::string::npos) {
      std::string key = line.substr(beginpos, seppos - beginpos);
      std::string val = line.substr(seppos + 1);
      trim(key);
      trim(val);

      auto& entries = properties[key];
      if(entries.length() > 0) {
        entries.append("\n");
      }
      entries.append(val);

      // Debugging Output
      // std::cout << key << ": " << val << std::endl;
    }
  }

  return properties;
}
