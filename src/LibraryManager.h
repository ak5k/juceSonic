#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <functional>

/**
 * @brief Generic library manager for ValueTree-based data
 *
 * LibraryManager manages multiple sub-libraries in a ValueTree structure.
 * It is format-agnostic and delegates parsing to provided parser functions.
 *
 * ValueTree structure:
 *   "Libraries" (root)
 *     - "SubLibrary" (identified by name)
 *       - property: "name" (library identifier)
 *       - children: library-specific data (format determined by parser)
 */
class LibraryManager
{
public:
    /**
     * @brief File parser function type
     * Takes a file and returns a ValueTree containing parsed data
     */
    using ParserFunction = std::function<juce::ValueTree(const juce::File&)>;

    /**
     * @brief Construct LibraryManager attached to a ValueTree
     * @param stateTree The parent state tree (e.g., from AudioProcessorValueTreeState)
     * @param propertyName The property name under which to store library data (default: "Libraries")
     */
    LibraryManager(juce::ValueTree stateTree, const juce::Identifier& propertyName = "Libraries");
    ~LibraryManager() = default;

    /**
     * @brief Load/update a sub-library from files using a custom parser
     * @param libraryName Unique identifier for this sub-library
     * @param files Array of files to load
     * @param parser Function to parse each file into ValueTree
     * @param clearExisting If true, clears existing data for this library before loading
     */
    void loadSubLibrary(
        const juce::String& libraryName,
        const juce::Array<juce::File>& files,
        ParserFunction parser,
        bool clearExisting = true
    );

    /**
     * @brief Scan directories and load files into a sub-library
     * @param libraryName Unique identifier for this sub-library
     * @param directories Array of directory paths to scan
     * @param filePattern Wildcard pattern for files (e.g., "*.rpl")
     * @param parser Function to parse each file into ValueTree
     * @param recursive Whether to scan subdirectories recursively
     * @param clearExisting If true, clears existing data for this library before loading
     */
    void scanAndLoadSubLibrary(
        const juce::String& libraryName,
        const juce::StringArray& directories,
        const juce::String& filePattern,
        ParserFunction parser,
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
     * @brief Get a specific sub-library by name
     * @param libraryName The name of the sub-library
     * @return ValueTree of the sub-library, or invalid tree if not found
     */
    juce::ValueTree getSubLibrary(const juce::String& libraryName) const;

    /**
     * @brief Check if a sub-library exists
     * @param libraryName The name of the sub-library
     */
    bool hasSubLibrary(const juce::String& libraryName) const;

    /**
     * @brief Get the number of sub-libraries
     */
    int getNumSubLibraries() const
    {
        return librariesTree.getNumChildren();
    }

    /**
     * @brief Clear a specific sub-library
     * @param libraryName The name of the sub-library to clear
     */
    void clearSubLibrary(const juce::String& libraryName);

    /**
     * @brief Clear all libraries
     */
    void clear();

private:
    juce::ValueTree parentState;
    juce::ValueTree librariesTree;

    // Helper to get or create a sub-library node
    juce::ValueTree getOrCreateSubLibrary(const juce::String& libraryName);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryManager)
};
