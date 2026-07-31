# Code-editing layer: one token vocabulary and one themed editor surface,
# shared by every language MAGDA shows in a CodeEditorComponent (MAGDA DSL,
# Faust, the chat prompt).
target_sources(magda_daw_app PRIVATE
    SyntaxTheme.cpp
    DSLTokeniser.cpp
    FaustTokeniser.cpp
    ChatPromptTokeniser.cpp
    SyntaxTheme.hpp
    DSLTokeniser.hpp
    FaustTokeniser.hpp
    ChatPromptTokeniser.hpp
)
