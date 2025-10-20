#include "ReaPackIndexParser.h"

std::vector<ReaPackIndexParser::JsfxEntry> ReaPackIndexParser::parseIndex(const juce::String& xmlContent) const
{
    std::vector<JsfxEntry> entries;
    lastError.clear();

    // Parse XML document
    std::unique_ptr<juce::XmlElement> rootElement = juce::parseXML(xmlContent);

    if (!rootElement)
    {
        lastError = "Failed to parse XML content";
        return entries;
    }

    // Verify root element is <index>
    if (rootElement->getTagName() != "index")
    {
        lastError = "Root element is not <index>";
        return entries;
    }

    // Check version (we support v1)
    juce::String version = rootElement->getStringAttribute("version", "1");
    if (version != "1")
    {
        lastError = "Unsupported index version: " + version;
        return entries;
    }

    // Parse all <category> elements
    for (auto* categoryElement : rootElement->getChildIterator())
        if (categoryElement->hasTagName("category"))
            parseCategory(categoryElement, entries);

    return entries;
}

std::vector<ReaPackIndexParser::JsfxEntry> ReaPackIndexParser::parseIndexFile(const juce::File& indexFile)
{
    if (!indexFile.existsAsFile())
    {
        lastError = "Index file does not exist: " + indexFile.getFullPathName();
        return {};
    }

    juce::String xmlContent = indexFile.loadFileAsString();
    return parseIndex(xmlContent);
}

juce::String ReaPackIndexParser::getRepositoryName(const juce::String& xmlContent)
{
    // Parse XML document
    std::unique_ptr<juce::XmlElement> rootElement = juce::parseXML(xmlContent);

    if (!rootElement || !rootElement->hasTagName("index"))
        return {};

    // Get the "name" attribute from the <index> element
    return rootElement->getStringAttribute("name");
}

void ReaPackIndexParser::parseCategory(juce::XmlElement* categoryElement, std::vector<JsfxEntry>& entries) const
{
    if (!categoryElement)
        return;

    juce::String categoryName = categoryElement->getStringAttribute("name", "Uncategorized");

    // Parse all <reapack> elements
    for (auto* reapackElement : categoryElement->getChildIterator())
        if (reapackElement->hasTagName("reapack"))
            parseReapack(reapackElement, categoryName, entries);
}

void ReaPackIndexParser::parseReapack(
    juce::XmlElement* reapackElement,
    const juce::String& categoryName,
    std::vector<JsfxEntry>& entries
) const
{
    if (!reapackElement)
        return;

    // Check if this is a JSFX entry (type="effect" or type="script")
    juce::String type = reapackElement->getStringAttribute("type");
    if (!isJsfxType(type))
        return;

    JsfxEntry entry;
    entry.name = reapackElement->getStringAttribute("name");
    entry.category = categoryName;

    // Parse <metadata> element (optional)
    if (auto* metadataElement = reapackElement->getChildByName("metadata"))
    {
        if (auto* descElement = metadataElement->getChildByName("description"))
        {
            // Get only the direct text content, not nested elements
            juce::String desc = descElement->getAllSubText().trim();

            // Clean up: remove CDATA markers if present
            desc = desc.replace("<![CDATA[", "");
            desc = desc.replace("]]>", "");

            // Skip RTF-formatted descriptions (they start with {\rtf)
            if (desc.startsWith("{\\rtf") || desc.contains("{\\colortbl"))
            {
                // Don't use RTF descriptions - they're too complex to parse
                entry.description = "";
            }
            else
            {
                // Remove control characters and escape sequences
                desc = desc.replaceCharacters("\r\n\t", "   ");

                // Remove common JSFX escape sequences like \r\t0
                desc = desc.replace("\\r", "");
                desc = desc.replace("\\n", " ");
                desc = desc.replace("\\t", " ");
                desc = desc.replace("\\0", "");

                // Remove multiple spaces
                while (desc.contains("  "))
                    desc = desc.replace("  ", " ");

                // Limit length to first sentence or reasonable length
                desc = desc.trim();

                // If description starts with "desc:" tag, extract just the value
                if (desc.startsWithIgnoreCase("desc:"))
                    desc = desc.substring(5).trim();

                entry.description = desc;
            }
        }
    }

    // Parse ALL <version> elements and find the one with the newest timestamp
    juce::XmlElement* latestVersionElement = nullptr;
    juce::String latestTimestamp;
    juce::String latestVersionName;

    for (auto* versionElement : reapackElement->getChildIterator())
    {
        if (versionElement->hasTagName("version"))
        {
            juce::String timestamp = versionElement->getStringAttribute("time");
            juce::String versionName = versionElement->getStringAttribute("name");

            // If this is the first version, or if it has a newer timestamp
            if (latestVersionElement == nullptr || timestamp > latestTimestamp)
            {
                latestVersionElement = versionElement;
                latestTimestamp = timestamp;
                latestVersionName = versionName;
            }
        }
    }

    // Parse the latest version (use timestamp for version tracking)
    if (latestVersionElement)
    {
        entry.version = latestVersionName; // Display version (e.g., "1.0.0")
        entry.timestamp = latestTimestamp; // Use timestamp for cache comparison
        entry.author = latestVersionElement->getStringAttribute("author");

        // Parse all <source> elements
        for (auto* sourceElement : latestVersionElement->getChildIterator())
        {
            if (sourceElement->hasTagName("source"))
            {
                SourceFile source;
                source.url = sourceElement->getAllSubText().trim();
                source.file = sourceElement->getStringAttribute("file", "");
                source.platform = sourceElement->getStringAttribute("platform", "all");

                if (source.url.isNotEmpty())
                {
                    entry.sources.push_back(source);

                    // Set main download URL to the first source (usually the main JSFX file)
                    if (entry.downloadUrl.isEmpty())
                        entry.downloadUrl = source.url;
                }
            }
        }
    }

    // Only add valid entries (must have name and at least one source)
    if (entry.isValid())
        entries.push_back(entry);
}

bool ReaPackIndexParser::isJsfxType(const juce::String& type)
{
    // ReaPack uses "effect" for JSFX effects and "script" for other JSFX
    return type == "effect" || type == "script";
}
