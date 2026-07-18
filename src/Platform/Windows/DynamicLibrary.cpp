#include "Platform/Windows/DynamicLibrary.h"

#include <filesystem>
#include <limits>
#include <utility>
#include <windows.h>

namespace aengine::detail
{
    namespace
    {
        std::wstring Utf8ToWide(std::string_view value)
        {
            if (value.empty() || value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
            {
                return {};
            }

            auto requiredSize = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                nullptr,
                0);
            if (requiredSize <= 0)
            {
                return {};
            }

            auto result = std::wstring(static_cast<std::size_t>(requiredSize), L'\0');
            auto convertedSize = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                value.data(),
                static_cast<int>(value.size()),
                result.data(),
                requiredSize);
            if (convertedSize != requiredSize)
            {
                return {};
            }

            return result;
        }

        std::filesystem::path GetExecutableDirectory()
        {
            auto buffer = std::vector<wchar_t>(MAX_PATH);
            while (true)
            {
                auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
                if (length == 0U)
                {
                    return {};
                }

                if (length < buffer.size())
                {
                    return std::filesystem::path(std::wstring(buffer.data(), length)).parent_path();
                }

                if (buffer.size() > static_cast<std::size_t>(std::numeric_limits<DWORD>::max()) / 2U)
                {
                    return {};
                }

                buffer.resize(buffer.size() * 2U);
            }
        }

        DynamicLibrary LoadFromDirectory(
            const std::filesystem::path& directory,
            std::initializer_list<std::wstring_view> libraryNames)
        {
            if (directory.empty())
            {
                return DynamicLibrary();
            }

            for (const auto libraryName : libraryNames)
            {
                auto candidate = directory / std::wstring(libraryName);
                if (auto handle = LoadLibraryW(candidate.c_str()); handle != nullptr)
                {
                    return DynamicLibrary(static_cast<void*>(handle));
                }
            }

            return DynamicLibrary();
        }
    }

    DynamicLibrary::DynamicLibrary()
        : Handle(nullptr)
    {
    }

    DynamicLibrary::DynamicLibrary(void* handle)
        : Handle(handle)
    {
    }

    DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept
        : Handle(std::exchange(other.Handle, nullptr))
    {
    }

    DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept
    {
        if (this != &other)
        {
            this->Reset();
            this->Handle = std::exchange(other.Handle, nullptr);
        }

        return *this;
    }

    DynamicLibrary::~DynamicLibrary()
    {
        this->Reset();
    }

    DynamicLibrary DynamicLibrary::LoadFromRuntimeSearchPath(
        std::initializer_list<std::wstring_view> libraryNames,
        const std::vector<std::string>& additionalSearchPaths)
    {
        for (const auto& searchPath : additionalSearchPaths)
        {
            auto wideSearchPath = Utf8ToWide(searchPath);
            if (wideSearchPath.empty())
            {
                continue;
            }

            auto library = LoadFromDirectory(std::filesystem::path(std::move(wideSearchPath)), libraryNames);
            if (library.IsLoaded())
            {
                return library;
            }
        }

        auto executableDirectory = GetExecutableDirectory();
        auto library = LoadFromDirectory(executableDirectory / L"ThirdParty", libraryNames);
        if (library.IsLoaded())
        {
            return library;
        }

        library = LoadFromDirectory(executableDirectory, libraryNames);
        if (library.IsLoaded())
        {
            return library;
        }

        for (const auto libraryName : libraryNames)
        {
            if (auto handle = LoadLibraryExW(
                std::wstring(libraryName).c_str(),
                nullptr,
                LOAD_LIBRARY_SEARCH_SYSTEM32); handle != nullptr)
            {
                return DynamicLibrary(static_cast<void*>(handle));
            }
        }

        return DynamicLibrary();
    }

    void DynamicLibrary::Abandon()
    {
        this->Handle = nullptr;
    }

    void DynamicLibrary::Reset()
    {
        if (this->Handle != nullptr)
        {
            FreeLibrary(static_cast<HMODULE>(this->Handle));
            this->Handle = nullptr;
        }
    }

    bool DynamicLibrary::IsLoaded() const
    {
        return this->Handle != nullptr;
    }

    void* DynamicLibrary::GetSymbol(const char* symbolName) const
    {
        if (this->Handle == nullptr || symbolName == nullptr)
        {
            return nullptr;
        }

        return reinterpret_cast<void*>(GetProcAddress(static_cast<HMODULE>(this->Handle), symbolName));
    }
}
