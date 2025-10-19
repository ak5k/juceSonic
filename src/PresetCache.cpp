#include "PresetCache.h"

void PresetCache::updateCache(const juce::ValueTree& newPresetsTree)
{
    const juce::ScopedWriteLock sl(lock);

    // Replace entire preset tree
    presetsTree = newPresetsTree.createCopy();

    // Notify listeners on message thread
    if (onCacheUpdated)
    {
        juce::MessageManager::callAsync(
            [this]()
            {
                if (onCacheUpdated)
                    onCacheUpdated();
            }
        );
    }
}

juce::ValueTree PresetCache::getPresetsTree() const
{
    const juce::ScopedReadLock sl(lock);
    return presetsTree.createCopy();
}

void PresetCache::clear()
{
    const juce::ScopedWriteLock sl(lock);
    presetsTree = juce::ValueTree("presets");

    // Notify listeners
    if (onCacheUpdated)
    {
        juce::MessageManager::callAsync(
            [this]()
            {
                if (onCacheUpdated)
                    onCacheUpdated();
            }
        );
    }
}

bool PresetCache::isEmpty() const
{
    const juce::ScopedReadLock sl(lock);
    return presetsTree.getNumChildren() == 0;
}

int PresetCache::getNumFiles() const
{
    const juce::ScopedReadLock sl(lock);
    return presetsTree.getNumChildren();
}
