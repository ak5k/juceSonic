#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

/**
 * Parses ReaPack XML index files to extract JSFX plugin information.
 *
 * ReaPack Index Format (v1):
 * <index version="1" name="ReaTeam JSFX">
 *   <category name="Effects">
 *     <reapack name="plugin_name.jsfx" type="effect">
 *       <metadata>
 *         <description>Plugin description</description>
 *       </metadata>
 *       <version name="1.0.0" author="Author Name">
 *         <source platform="all">https://example.com/plugin.jsfx</source>
 *       </version>
 *     </reapack>
 *   </category>
 * </index>
 */
class ReaPackIndexParser
{
public:
    struct SourceFile
    {
        juce::String url;      // Download URL
        juce::String file;     // Relative file path (e.g., "graphics/knob.png")
        juce::String platform; // Platform ("all", "windows", "darwin", "linux")
    };

    struct JsfxEntry
    {
        juce::String name;               // Plugin name (e.g., "compressor.jsfx")
        juce::String category;           // Category (e.g., "Effects", "Delay")
        juce::String author;             // Author name
        juce::String version;            // Version string (e.g., "1.0.0")
        juce::String timestamp;          // ISO timestamp for cache comparison (e.g., "2024-10-28T19:21:56Z")
        juce::String description;        // Plugin description
        juce::String downloadUrl;        // Main JSFX file download URL (for backward compatibility)
        std::vector<SourceFile> sources; // All source files (JSFX + graphics/data files)

        bool isValid() const
        {
            return name.isNotEmpty() && !sources.empty();
        }
    };

    ReaPackIndexParser() = default;
    ~ReaPackIndexParser() = default;

    /**
     * Parse a ReaPack XML index from string content.
     * @param xmlContent The XML content as string
     * @return Vector of parsed JSFX entries (only type="effect" or type="script")
     */
    std::vector<JsfxEntry> parseIndex(const juce::String& xmlContent) const;

    /**
     * Parse a ReaPack XML index from file.
     * @param indexFile Path to the XML index file
     * @return Vector of parsed JSFX entries
     */
    std::vector<JsfxEntry> parseIndexFile(const juce::File& indexFile);

    /**
     * Extract repository name from index XML content.
     * @param xmlContent The XML content as string
     * @return Repository name from the "name" attribute, or empty if not found
     */
    static juce::String getRepositoryName(const juce::String& xmlContent);

    /**
     * Get the last error message if parsing failed.
     */
    juce::String getLastError() const
    {
        return lastError;
    }

private:
    mutable juce::String lastError;

    void parseCategory(juce::XmlElement* categoryElement, std::vector<JsfxEntry>& entries) const;
    void parseReapack(
        juce::XmlElement* reapackElement,
        const juce::String& categoryName,
        std::vector<JsfxEntry>& entries
    ) const;
    static bool isJsfxType(const juce::String& type);
};
