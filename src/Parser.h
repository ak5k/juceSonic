#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

/**
 * @brief Base class for parsing library files into ValueTree structures
 *
 * Each parser implementation handles a specific file format and converts
 * it into a standardized ValueTree structure that LibraryManager can work with.
 */
class Parser
{
public:
    virtual ~Parser() = default;

    /**
     * @brief Parse a file and return its contents as a ValueTree
     * @param file The file to parse
     * @return ValueTree representing the parsed data, or invalid tree on failure
     */
    virtual juce::ValueTree parseFile(const juce::File& file) = 0;

    /**
     * @brief Get the file extension this parser handles (e.g., "rpl")
     * @return File extension without the dot
     */
    virtual juce::String getFileExtension() const = 0;

protected:
    Parser() = default;
};
