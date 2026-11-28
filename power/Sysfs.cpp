#include "Sysfs.h"

#include <android-base/file.h>
#include <android-base/logging.h>
#include <android-base/strings.h>
#include <android-base/unique_fd.h>

#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <memory>

namespace pc::power {

std::string readValue(const std::string& path) {
    std::string text;
    if (!android::base::ReadFileToString(path, &text)) return "";
    return android::base::Trim(text);
}

std::vector<std::string> readChoices(const std::string& path) {
    std::vector<std::string> choices;
    for (std::string word : android::base::Split(readValue(path), " ")) {
        word.erase(std::remove(word.begin(), word.end(), '['), word.end());
        word.erase(std::remove(word.begin(), word.end(), ']'), word.end());
        if (!word.empty()) choices.push_back(word);
    }
    return choices;
}

std::string readCurrentChoice(const std::string& path) {
    const std::string text = readValue(path);
    const size_t open = text.find('[');
    const size_t close = text.find(']', open);
    if (open == std::string::npos || close == std::string::npos) return text;
    return text.substr(open + 1, close - open - 1);
}

bool writeValue(const std::string& path, const std::string& value) {
    if (readCurrentChoice(path) == value) return true;
    android::base::unique_fd fd(TEMP_FAILURE_RETRY(open(path.c_str(), O_WRONLY | O_CLOEXEC)));
    if (fd < 0) {
        if (errno != ENOENT) PLOG(WARNING) << "cannot open " << path;
        return false;
    }
    if (!android::base::WriteStringToFd(value, fd)) {
        PLOG(WARNING) << path << " refused " << value;
        return false;
    }
    return true;
}

bool exists(const std::string& path) {
    return access(path.c_str(), F_OK) == 0;
}

std::vector<std::string> listDir(const std::string& dir) {
    std::vector<std::string> names;
    std::unique_ptr<DIR, decltype(&closedir)> handle(opendir(dir.c_str()), closedir);
    if (!handle) return names;
    for (dirent* e = readdir(handle.get()); e; e = readdir(handle.get())) {
        if (e->d_name[0] != '.') names.emplace_back(e->d_name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::string firstOffered(const std::vector<std::string>& candidates,
                         const std::vector<std::string>& choices) {
    for (const std::string& candidate : candidates) {
        if (contains(choices, candidate)) return candidate;
    }
    return "";
}

bool contains(const std::vector<std::string>& list, const std::string& value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

}  // namespace pc::power
