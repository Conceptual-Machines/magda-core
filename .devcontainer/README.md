# Development Container for Magda

This directory contains the configuration for using Visual Studio Code's Dev Containers feature or GitHub Codespaces with the Magda project.

## What is a Dev Container?

A development container (or dev container for short) allows you to use a Docker container as a full-featured development environment. It includes:

- All build dependencies pre-installed (C++ compiler, CMake, JUCE dependencies, etc.)
- Configured C++ development tools (clangd, CMake Tools)
- GitHub Copilot integration
- Consistent development environment across different machines

## Prerequisites

### For VS Code Dev Containers:
1. [Docker Desktop](https://www.docker.com/products/docker-desktop) installed and running
2. [Visual Studio Code](https://code.visualstudio.com/)
3. [Dev Containers extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers) for VS Code

### For GitHub Codespaces:
- Just a web browser! GitHub Codespaces provides a cloud-based development environment.

## Getting Started

### Option 1: Using VS Code Dev Containers (Local)

1. Open VS Code
2. Install the "Dev Containers" extension (ms-vscode-remote.remote-containers)
3. Open the magda-core folder in VS Code
4. When prompted, click "Reopen in Container" (or use Command Palette: `Dev Containers: Reopen in Container`)
5. Wait for the container to build and start (first time will take a few minutes)
6. Once ready, run:
   ```bash
   make setup    # Initialize submodules and dependencies
   make debug    # Build the project
   ```

### Option 2: Using GitHub Codespaces (Cloud)

1. Go to the [magda-core repository](https://github.com/Conceptual-Machines/magda-core) on GitHub
2. Click the green "Code" button
3. Select the "Codespaces" tab
4. Click "Create codespace on main" (or your desired branch)
5. Wait for the codespace to initialize
6. Once ready, run:
   ```bash
   make setup    # Initialize submodules and dependencies
   make debug    # Build the project
   ```

## What's Included

The dev container includes:

### Build Tools
- GCC/G++ compiler with C++20 support
- CMake 3.20+
- Ninja build system
- pkg-config

### Audio Development Libraries
- JUCE dependencies (GTK3, ALSA, JACK)
- X11 development libraries
- WebKit2GTK for browser components

### Code Quality Tools
- clang-format for code formatting
- clang-tidy for static analysis
- pre-commit hooks support

### VS Code Extensions
- **clangd**: C++ language server for IntelliSense
- **CMake Tools**: CMake integration
- **GitHub Copilot**: AI pair programming
- **GitLens**: Enhanced Git integration
- **Code Spell Checker**: Spell checking for code

## Configuration

The devcontainer uses the existing `Dockerfile` in the root of the repository, ensuring consistency between local Docker builds and the dev container environment.

### Key Settings

- **Working Directory**: `/workspace` (mounted to your local project directory)
- **CMake Generator**: Ninja
- **Build Directory**: `${workspaceFolder}/build`
- **C++ Standard**: C++20
- **Default Build Type**: Debug

## Building and Testing

Once inside the dev container, you can use the standard make commands:

```bash
# Setup project
make setup

# Build in debug mode
make debug

# Build in release mode
make release

# Run tests
make test

# Format code
make format

# Run linter
make lint

# Clean build artifacts
make clean
```

## Troubleshooting

### Container fails to build
- Make sure Docker is running
- Check that you have enough disk space
- Try rebuilding: Command Palette → `Dev Containers: Rebuild Container`

### clangd not working
- Run `make debug` first to generate `compile_commands.json`
- The compile commands file is needed for clangd to understand the project structure

### Submodules not initialized
- Run `make setup` to initialize git submodules (Tracktion Engine, JUCE)

### Permission issues
- The container runs as root by default, which should prevent most permission issues
- If you encounter permission issues, check file ownership and consider using `chown`

## Advanced Usage

### Customizing the Container

You can customize the dev container by modifying `.devcontainer/devcontainer.json`. For example:

- Add more VS Code extensions
- Change build settings
- Add more tools or dependencies
- Configure environment variables

### Using with Docker Compose

If you need additional services (like a database), you can switch to using Docker Compose by creating a `docker-compose.yml` file and updating the devcontainer.json to reference it.

## More Information

- [VS Code Dev Containers documentation](https://code.visualstudio.com/docs/devcontainers/containers)
- [GitHub Codespaces documentation](https://docs.github.com/en/codespaces)
- [Magda project README](../README.md)
