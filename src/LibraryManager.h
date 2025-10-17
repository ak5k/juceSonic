#pragma once

#include "PresetConverter.h"

#include <juce_data_structures/juce_data_structures.h>
#include <memory>

/**
 * @brief Generic library manager for ValueTree-based preset data
 *
 * LibraryManager is the public interface for managing preset libraries.
 * It uses the Strategy pattern (via PresetConverter) to support different
 * preset formats without knowing their implementation details.
 *
 * ValueTree structure:
 *   "Libraries" (root)
 *     - "Library" (identified by name)
 *       - property: "name" (library identifier)
 *       - property: "path" (source directory path, optional)
 *       - children: PresetFile nodes from converter
 *         - PresetFile (from converter)
 *           - PresetBank (from converter)
 *             - Preset (from converter, with "data" property)
 */
class LibraryManager
{
public:
    /**
     * @brief Construct LibraryManager attached to a ValueTree
     * @param stateTree The parent state tree (e.g., from AudioProcessorValueTreeState)
     * @param propertyName The property name under which to store library data (default: "Libraries")
     */
    LibraryManager(juce::ValueTree stateTree, const juce::Identifier& propertyName = "Libraries");
    ~LibraryManager() = default;

    /**
     * @brief Prepare a library with a specific converter
     *
     * This sets up a library to use a specific preset format converter.
     * Subsequent loadLibrary calls will use this converter.
     *
     * @param libraryName Unique identifier for this library
     * @param converter The converter to use for this library (ownership transferred)
     */
    void prepareLibrary(const juce::String& libraryName, std::unique_ptr<PresetConverter> converter);

    /**
     * @brief Load/update a library from a directory path
     *
     * Uses the converter set via prepareLibrary to parse files.
     * Scans the directory for files matching the converter's supported extensions.
     *
     * @param libraryName Unique identifier for this library
     * @param directoryPath Path to directory containing preset files
     * @param recursive Whether to scan subdirectories recursively
     * @param clearExisting If true, clears existing data before loading
     * @return Number of files successfully loaded
     */
    int loadLibrary(
        const juce::String& libraryName,
        const juce::String& directoryPath,
        bool recursive = true,
        bool clearExisting = true
    );

    /**
     * @brief Load/update a library from multiple directory paths
     *
     * @param libraryName Unique identifier for this library
    /**
     * @brief Load/update a library from multiple directory paths
     *
     * @param libraryName Unique identifier for this library
     * @param directoryPaths Array of directory paths to scan
     * @param recursive Whether to scan subdirectories recursively
     * @param clearExisting If true, clears existing data before loading
     * @return Number of files successfully loaded
     */
    int loadLibrary(
        const juce::String& libraryName,
        const juce::StringArray& directoryPaths,
        bool recursive = true,
        bool clearExisting = true
    );

    /**
     * @brief Get the root libraries ValueTree (read-only)
     */
    const juce::ValueTree& getLibraries() const
    {
        return librariesTree;
    }

    /**
     * @brief Get a specific library by name
     * @param libraryName The name of the library
     * @return ValueTree of the library, or invalid tree if not found
     */
    juce::ValueTree getLibrary(const juce::String& libraryName) const;

    /**
     * @brief Check if a library exists
     * @param libraryName The name of the library
     */
    bool hasLibrary(const juce::String& libraryName) const;

    /**
     * @brief Get the number of libraries
     */
    int getNumLibraries() const
    {
        return librariesTree.getNumChildren();
    }

    /**
     * @brief Clear a specific library
     * @param libraryName The name of the library to clear
     */
    void clearLibrary(const juce::String& libraryName);

    /**
     * @brief Clear all libraries
     */
    void clear();

    /**
     * @brief Get the converter for a specific library
     * @param libraryName The name of the library
     * @return Pointer to converter, or nullptr if not set
     */
    PresetConverter* getConverter(const juce::String& libraryName) const;

private:
    juce::ValueTree parentState;
    juce::ValueTree librariesTree;

    // Map of library name -> converter
    std::map<juce::String, std::unique_ptr<PresetConverter>> converters;

    // Helper to get or create a library node
    juce::ValueTree getOrCreateLibrary(const juce::String& libraryName);

    // Helper to scan files in a directory matching converter's extensions
    juce::Array<juce::File> scanFiles(const juce::String& directoryPath, PresetConverter* converter, bool recursive);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryManager)
};
