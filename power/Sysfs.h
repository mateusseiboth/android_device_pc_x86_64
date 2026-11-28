// Small helpers for the sysfs knobs the Power HAL reads and writes. A PC has only some of them, so a
// missing file is never an error: the caller simply skips that knob.
#pragma once

#include <string>
#include <vector>

namespace pc::power {

// The file's contents without surrounding whitespace; empty when it is missing.
std::string readValue(const std::string& path);

// The current choice of a file that lists its choices with the current one in brackets
// ("[default] performance powersave"); the plain value for any other file.
std::string readCurrentChoice(const std::string& path);

// Every choice a list file offers, brackets removed.
std::vector<std::string> readChoices(const std::string& path);

// Writes the value unless the file already holds it. A missing file returns false quietly; a refused
// write (the driver does not take that value) is logged and returns false.
bool writeValue(const std::string& path, const std::string& value);

bool exists(const std::string& path);

// Entry names of a directory, dot entries left out; empty when it is missing.
std::vector<std::string> listDir(const std::string& dir);

// The first candidate found among the choices, or empty.
std::string firstOffered(const std::vector<std::string>& candidates,
                         const std::vector<std::string>& choices);

bool contains(const std::vector<std::string>& list, const std::string& value);

}  // namespace pc::power
