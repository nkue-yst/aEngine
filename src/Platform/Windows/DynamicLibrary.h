#pragma once

#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace aengine::detail
{
    class DynamicLibrary final
    {
    public:
        DynamicLibrary();
        explicit DynamicLibrary(void* handle);
        DynamicLibrary(const DynamicLibrary&) = delete;
        DynamicLibrary& operator=(const DynamicLibrary&) = delete;
        DynamicLibrary(DynamicLibrary&& other) noexcept;
        DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;
        ~DynamicLibrary();

        [[nodiscard]] static DynamicLibrary LoadFromRuntimeSearchPath(
            std::initializer_list<std::wstring_view> libraryNames,
            const std::vector<std::string>& additionalSearchPaths);

        void Abandon();
        void Reset();
        [[nodiscard]] bool IsLoaded() const;
        [[nodiscard]] void* GetSymbol(const char* symbolName) const;

        template<typename T>
        [[nodiscard]] bool LoadSymbol(const char* symbolName, T& target) const
        {
            target = reinterpret_cast<T>(this->GetSymbol(symbolName));
            return target != nullptr;
        }

    private:
        void* Handle;
    };
}
