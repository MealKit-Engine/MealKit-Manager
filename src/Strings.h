#pragma once

// Screen text: English source in code, Korean in the table in Strings.cpp (same approach as the engine editor's tr()). The language comes from the language line in editor settings.
#include <string>

namespace mkmanager {

void setLanguage(const std::string& language); // "ko", "en"
const char* tr(const char* english);
std::string trf(const char* english, ...); // translates, then fills in as a printf format

} // namespace mkmanager
