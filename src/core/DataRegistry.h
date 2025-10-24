#pragma once

#include "Types.h"

#include <atomic>
#include <functional>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace core {

class DataRegistry {
public:
    using Observer = std::function<void(const DataFrame&)>;

    void registerSource(SourceMetadata metadata);
    void unregisterSource(const std::string& sourceId);

    [[nodiscard]] bool isRegistered(const std::string& sourceId) const;
    [[nodiscard]] std::optional<SourceMetadata> metadata(const std::string& sourceId) const;
    [[nodiscard]] std::vector<SourceMetadata> listSources() const;

    void update(const DataFrame& frame);
    [[nodiscard]] std::optional<DataFrame> latest(const std::string& sourceId) const;

    int addObserver(const std::string& sourceId, Observer observer);
    void removeObserver(const std::string& sourceId, int token);

private:
    struct ObserverEntry {
        int id;
        Observer callback;
    };

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, SourceMetadata> metadata_;
    std::unordered_map<std::string, DataFrame> latestFrames_;
    std::unordered_map<std::string, std::vector<ObserverEntry>> observers_;
    std::atomic<int> nextObserverId_{1};
};

}  // namespace core
