#include "DawProjectValidator.hpp"

#include <libxml/parser.h>
#include <libxml/xmlschemas.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace magda {
namespace {

juce::File schemaDirectory() {
    // Released builds carry the XSDs next to the binary (Contents/Resources on
    // macOS, exe-adjacent elsewhere) via CMake's POST_BUILD copy + install rules.
    // Fall back to the build-tree source dir for dev runs and unit tests, where
    // nothing is staged alongside the executable.
    auto exe = juce::File::getSpecialLocation(juce::File::currentApplicationFile);
#if JUCE_MAC
    auto bundled = exe.getChildFile("Contents/Resources/dawproject");
#else
    auto bundled = exe.getParentDirectory().getChildFile("dawproject");
#endif
    if (bundled.isDirectory())
        return bundled;

    return juce::File(MAGDA_DAWPROJECT_SCHEMA_DIR);
}

void appendLibXmlError(void* context, const char* message, ...) {
    if (context == nullptr || message == nullptr)
        return;

    char buffer[2048];
    va_list args;
    va_start(args, message);
    std::vsnprintf(buffer, sizeof(buffer), message, args);
    va_end(args);

    auto& error = *static_cast<juce::String*>(context);
    error += juce::String(buffer).trim();
    if (!error.endsWithChar('\n'))
        error += "\n";
}

}  // namespace

bool DawProjectValidator::validateProjectXml(const juce::String& xml, juce::String& error) {
    return validateXmlAgainstSchema(xml, schemaDirectory().getChildFile("Project.xsd"), error);
}

bool DawProjectValidator::validateMetadataXml(const juce::String& xml, juce::String& error) {
    return validateXmlAgainstSchema(xml, schemaDirectory().getChildFile("MetaData.xsd"), error);
}

bool DawProjectValidator::validateXmlAgainstSchema(const juce::String& xml,
                                                   const juce::File& schemaFile,
                                                   juce::String& error) {
    error.clear();

    if (xml.trim().isEmpty()) {
        error = "XML content is empty";
        return false;
    }

    if (!schemaFile.existsAsFile()) {
        error = "DAWproject schema not found: " + schemaFile.getFullPathName();
        return false;
    }

    xmlInitParser();

    auto* parserContext = xmlSchemaNewParserCtxt(schemaFile.getFullPathName().toRawUTF8());
    if (parserContext == nullptr) {
        error = "Could not create XML schema parser context";
        return false;
    }

    xmlSchemaSetParserErrors(parserContext, appendLibXmlError, appendLibXmlError, &error);
    auto* schema = xmlSchemaParse(parserContext);
    xmlSchemaFreeParserCtxt(parserContext);

    if (schema == nullptr) {
        if (error.isEmpty())
            error = "Could not parse DAWproject schema: " + schemaFile.getFullPathName();
        return false;
    }

    const auto* utf8 = xml.toRawUTF8();
    auto* document =
        xmlReadMemory(utf8, static_cast<int>(std::strlen(utf8)), nullptr, nullptr, XML_PARSE_NONET);
    if (document == nullptr) {
        xmlSchemaFree(schema);
        error = "Could not parse XML content";
        return false;
    }

    auto* validationContext = xmlSchemaNewValidCtxt(schema);
    if (validationContext == nullptr) {
        xmlFreeDoc(document);
        xmlSchemaFree(schema);
        error = "Could not create XML schema validation context";
        return false;
    }

    xmlSchemaSetValidErrors(validationContext, appendLibXmlError, appendLibXmlError, &error);
    const auto result = xmlSchemaValidateDoc(validationContext, document);

    xmlSchemaFreeValidCtxt(validationContext);
    xmlFreeDoc(document);
    xmlSchemaFree(schema);

    if (result == 0)
        return true;

    if (error.isEmpty())
        error = result > 0 ? "XML did not validate against DAWproject schema"
                           : "XML schema validation failed";
    return false;
}

}  // namespace magda
