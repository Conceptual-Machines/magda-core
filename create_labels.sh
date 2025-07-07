#!/bin/bash

# Create GitHub labels for Magica project
# Usage: ./create_labels.sh

echo "Creating GitHub labels for Magica project..."

# Priority Labels
gh label create "High Priority" --color "d73a4a" --description "High priority issues"
gh label create "Medium Priority" --color "fbca04" --description "Medium priority issues"
gh label create "Low Priority" --color "0e8a16" --description "Low priority issues"

# Domain Labels
gh label create "build-system" --color "1d76db" --description "Build system and CMake issues"
gh label create "ui" --color "c2e0c6" --description "User interface components"
gh label create "daw-engine" --color "d93f0b" --description "DAW engine and audio processing"
gh label create "grpc" --color "5319e7" --description "gRPC and network communication"
gh label create "server" --color "fef2c0" --description "Server implementation"

# Feature Labels
gh label create "transport" --color "c2e0c6" --description "Transport controls and playback"
gh label create "tracks" --color "c2e0c6" --description "Track management"
gh label create "timeline" --color "c2e0c6" --description "Timeline and timeline view"
gh label create "mixer" --color "c2e0c6" --description "Mixer and audio routing"
gh label create "clips" --color "c2e0c6" --description "MIDI and audio clips"
gh label create "core" --color "c2e0c6" --description "Core functionality"

# Quality Labels
gh label create "testing" --color "fef2c0" --description "Testing and test infrastructure"
gh label create "unit-tests" --color "fef2c0" --description "Unit tests"
gh label create "integration-tests" --color "fef2c0" --description "Integration tests"
gh label create "error-handling" --color "d93f0b" --description "Error handling and validation"
gh label create "validation" --color "d93f0b" --description "Input validation and error checking"

# DevOps Labels
gh label create "ci-cd" --color "1d76db" --description "Continuous integration and deployment"
gh label create "automation" --color "1d76db" --description "Automation and scripting"
gh label create "performance" --color "fbca04" --description "Performance optimization"
gh label create "monitoring" --color "fbca04" --description "Monitoring and metrics"

# Documentation and Features
gh label create "documentation" --color "0075ca" --description "Documentation updates"
gh label create "plugins" --color "5319e7" --description "Plugin system and extensibility"
gh label create "extensibility" --color "5319e7" --description "Extensibility features"

# Technology Labels
gh label create "cmake" --color "1d76db" --description "CMake build system"
gh label create "juce" --color "c2e0c6" --description "JUCE framework"
gh label create "tracktion" --color "d93f0b" --description "Tracktion Engine"

echo "All labels created successfully!" 