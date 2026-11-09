#pragma once
#include <QString>
#include <filesystem>

inline QString pathText(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(value.data()), qsizetype(value.size()));
}
inline std::filesystem::path nativePath(const QString& value) {
#ifdef _WIN32
    return std::filesystem::path(value.toStdWString());
#else
    const auto bytes = value.toUtf8();
    return std::filesystem::u8path(bytes.constData(), bytes.constData() + bytes.size());
#endif
}
