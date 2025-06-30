#pragma once

#include <string>
#include <functional>

/**
 * @brief Interface for interacting with language models and AI assistants
 * 
 * The PromptInterface provides a bridge between DAW operations and 
 * language models, allowing agents to get contextual help, generate
 * musical ideas, or interpret natural language commands.
 */
class PromptInterface {
public:
    virtual ~PromptInterface() = default;
    
    /**
     * @brief Send a prompt to a language model
     * @param prompt The text prompt to send
     * @param context Optional context about current DAW state
     * @return The model's response
     */
    virtual std::string sendPrompt(const std::string& prompt, 
                                  const std::string& context = "") = 0;
    
    /**
     * @brief Send a prompt asynchronously
     * @param prompt The text prompt to send
     * @param callback Function to call with the response
     * @param context Optional context about current DAW state
     */
    virtual void sendPromptAsync(const std::string& prompt,
                                std::function<void(const std::string&)> callback,
                                const std::string& context = "") = 0;
    
    /**
     * @brief Generate musical suggestions based on current state
     * @param style Musical style or genre
     * @param current_progression Current chord progression or musical context
     * @return Suggested musical elements (chords, melodies, etc.)
     */
    virtual std::string generateMusicalSuggestion(const std::string& style,
                                                 const std::string& current_progression = "") = 0;
    
    /**
     * @brief Convert natural language to DAW commands
     * @param natural_language User's natural language input
     * @return Suggested command or action
     */
    virtual std::string interpretCommand(const std::string& natural_language) = 0;
    
    /**
     * @brief Get help about available commands
     * @param topic Optional specific topic to get help about
     * @return Help text
     */
    virtual std::string getHelp(const std::string& topic = "") = 0;
    
    /**
     * @brief Set the current DAW context for better responses
     * @param context_info JSON string with current DAW state
     */
    virtual void setContext(const std::string& context_info) = 0;
}; 