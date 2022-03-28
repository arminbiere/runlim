#pragma once

#include <string>
#include <unordered_map>
#include <vector>

std::pair<std::string, int>
exec(const char* cmd);

std::unordered_map<std::string, std::string>
parse_runlim_prints(const std::string& str);
