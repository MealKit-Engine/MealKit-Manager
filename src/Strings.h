#pragma once

// 화면 글자: 코드에는 영어 원문, 한국어는 Strings.cpp의 표 (엔진 에디터 tr()과 같은 방식). 언어는 에디터 설정의 language 줄.
#include <string>

namespace mkmanager {

void setLanguage(const std::string& language); // "ko", "en"
const char* tr(const char* english);
std::string trf(const char* english, ...); // 번역한 뒤 printf 형식으로 채운다

} // namespace mkmanager
