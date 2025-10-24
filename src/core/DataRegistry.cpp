#include "DataRegistry.h"
#include <spdlog/spdlog.h>

#include <algorithm>

namespace core {

void DataRegistry::registerSource(SourceMetadata metadata)
{
    std::unique_lock lock(mutex_);
    metadata_[metadata.id] = std::move(metadata);
}

void DataRegistry::unregisterSource(const std::string& sourceId)
{
    std::unique_lock lock(mutex_);
    metadata_.erase(sourceId);
    latestFrames_.erase(sourceId);
    observers_.erase(sourceId);
}

bool DataRegistry::isRegistered(const std::string& sourceId) const
{
    std::shared_lock lock(mutex_);
    return metadata_.find(sourceId) != metadata_.end();
}

std::optional<SourceMetadata> DataRegistry::metadata(const std::string& sourceId) const
{
    std::shared_lock lock(mutex_);
    if (auto it = metadata_.find(sourceId); it != metadata_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<SourceMetadata> DataRegistry::listSources() const
{
    std::shared_lock lock(mutex_);
    std::vector<SourceMetadata> result;
    result.reserve(metadata_.size());
    for (const auto& [_, meta] : metadata_) {
        result.push_back(meta);
    }
    return result;
}

void DataRegistry::update(const DataFrame& frame)
{
    std::vector<Observer> callbacks;
    {
        std::unique_lock lock(mutex_);
        latestFrames_[frame.sourceId] = frame;
        if (auto it = observers_.find(frame.sourceId); it != observers_.end()) {
            callbacks.reserve(it->second.size());
            for (const auto& entry : it->second) {
                callbacks.push_back(entry.callback);
            }
        }
    }
    spdlog::trace("DataRegistry: update for source '{}' with {} points", frame.sourceId, frame.points.size());
    for (const auto& cb : callbacks) {
        if (cb) {
            cb(frame);
        }
    }
}

std::optional<DataFrame> DataRegistry::latest(const std::string& sourceId) const
{
    std::shared_lock lock(mutex_);
    if (auto it = latestFrames_.find(sourceId); it != latestFrames_.end()) {
        return it->second;
    }
    return std::nullopt;
}

int DataRegistry::addObserver(const std::string& sourceId, Observer observer)
{
    std::unique_lock lock(mutex_);
    const int token = nextObserverId_++;
    observers_[sourceId].push_back(ObserverEntry { token, std::move(observer) });
    return token;
}

void DataRegistry::removeObserver(const std::string& sourceId, int token)
{
    std::unique_lock lock(mutex_);
    if (auto it = observers_.find(sourceId); it != observers_.end()) {
        auto& entries = it->second;
        entries.erase(std::remove_if(entries.begin(), entries.end(), [token](const ObserverEntry& entry) {
            return entry.id == token;
        }),
            entries.end());
        if (entries.empty()) {
            observers_.erase(it);
        }
    }
}

} // namespace core
