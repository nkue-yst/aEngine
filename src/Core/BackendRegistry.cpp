#include <aengine/aengine.h>

#include <map>
#include <utility>

namespace aengine
{
    namespace
    {
        struct Registration final
        {
            Registration(std::string name, BackendCapabilities capabilities, BackendRegistry::Factory factory)
                : Name(std::move(name))
                , Capabilities(capabilities)
                , Create(std::move(factory))
            {
            }

            std::string Name;
            BackendCapabilities Capabilities;
            BackendRegistry::Factory Create;
        };
    }

    class BackendRegistry::Implementation final
    {
    public:
        Implementation()
            : Registrations()
        {
        }

        std::map<BackendId, Registration> Registrations;
    };

    BackendRegistry::BackendRegistry()
        : State(std::make_unique<Implementation>())
    {
    }

    BackendRegistry::~BackendRegistry() = default;

    BackendRegistry::BackendRegistry(BackendRegistry&& other) noexcept = default;

    BackendRegistry& BackendRegistry::operator=(BackendRegistry&& other) noexcept = default;

    Result BackendRegistry::Register(BackendId id, std::string name, BackendCapabilities capabilities, Factory factory)
    {
        if (name.empty() || !factory)
        {
            return std::unexpected(Error(ErrorCode::InvalidArgument, "Backend registration requires a name and a factory."));
        }

        if (this->State->Registrations.contains(id))
        {
            return std::unexpected(Error(ErrorCode::InvalidArgument, "The backend is already registered."));
        }

        this->State->Registrations.emplace(id, Registration(std::move(name), capabilities, std::move(factory)));
        return {};
    }

    std::vector<BackendId> BackendRegistry::Backends() const
    {
        auto result = std::vector<BackendId>();
        result.reserve(this->State->Registrations.size());
        for (const auto& [id, registration] : this->State->Registrations)
        {
            static_cast<void>(registration);
            result.emplace_back(id);
        }
        return result;
    }

    std::string_view BackendRegistry::Name(BackendId id) const
    {
        auto iterator = this->State->Registrations.find(id);
        return iterator == this->State->Registrations.end()
            ? std::string_view()
            : std::string_view(iterator->second.Name);
    }

    Expected<BackendCapabilities> BackendRegistry::Capabilities(BackendId id) const
    {
        auto iterator = this->State->Registrations.find(id);
        if (iterator == this->State->Registrations.end())
        {
            return std::unexpected(Error(ErrorCode::BackendUnavailable, "The requested backend is not registered."));
        }

        return iterator->second.Capabilities;
    }

    Expected<std::unique_ptr<IAudioEngine>> BackendRegistry::Create(BackendId id, const AudioEngineCreateInfo& createInfo) const
    {
        auto iterator = this->State->Registrations.find(id);
        if (iterator == this->State->Registrations.end())
        {
            return std::unexpected(Error(ErrorCode::BackendUnavailable, "The requested backend is not registered."));
        }

        auto engine = iterator->second.Create();
        if (engine == nullptr)
        {
            return std::unexpected(Error(ErrorCode::InitializationFailed, "The backend factory returned no audio engine."));
        }

        auto initialization = engine->Initialize(createInfo);
        if (!initialization)
        {
            return std::unexpected(std::move(initialization.error()));
        }

        return std::move(engine);
    }
}
