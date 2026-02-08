# Development Container Setup Guide

This guide explains how to use the development container for the Magda project.

## What is a Development Container?

A development container (devcontainer) is a Docker container configured with all the tools, libraries, and runtime dependencies needed for development. This ensures:

✅ **Consistency**: Everyone works in the same environment  
✅ **Quick Setup**: No manual installation of dependencies  
✅ **Isolation**: Development environment doesn't affect your local system  
✅ **GitHub Copilot Integration**: Pre-configured for AI-assisted coding  

## Prerequisites

### For Local Development (VS Code)
- Docker Desktop installed and running
- Visual Studio Code
- Dev Containers extension for VS Code

### For Cloud Development (GitHub Codespaces)
- Just a web browser!
- GitHub account with Codespaces access

## Getting Started with GitHub Codespaces

GitHub Codespaces provides a complete, cloud-based development environment:

1. **Navigate** to https://github.com/Conceptual-Machines/magda-core
2. **Click** the green "Code" button
3. **Select** the "Codespaces" tab
4. **Click** "Create codespace on main" (or your branch)
5. **Wait** for initialization (2-3 minutes first time)
6. **Run** the following commands in the integrated terminal:
   ```bash
   make setup    # Initialize git submodules
   make debug    # Build the project
   make test     # Run tests
   ```

### Codespaces Tips
- Your work is automatically saved to the cloud
- You can have multiple codespaces (one per branch recommended)
- Stop/start codespaces to save compute hours
- Use "View → Command Palette → Codespaces: Stop Current Codespace" when done

## Getting Started with VS Code Dev Containers

For local development with the same containerized environment:

1. **Install Prerequisites**:
   - [Docker Desktop](https://www.docker.com/products/docker-desktop)
   - [Visual Studio Code](https://code.visualstudio.com/)
   - [Dev Containers Extension](https://marketplace.visualstudio.com/items?itemName=ms-vscode-remote.remote-containers)

2. **Clone the Repository**:
   ```bash
   git clone --recursive https://github.com/Conceptual-Machines/magda-core.git
   cd magda-core
   ```

3. **Open in VS Code**:
   ```bash
   code .
   ```

4. **Reopen in Container**:
   - VS Code will detect the `.devcontainer` folder
   - Click "Reopen in Container" when prompted
   - Or use Command Palette (F1): `Dev Containers: Reopen in Container`

5. **Wait for Container Build** (5-10 minutes first time):
   - The Dockerfile will be built
   - Extensions will be installed
   - Environment will be configured

6. **Start Developing**:
   ```bash
   make setup    # Initialize git submodules
   make debug    # Build the project
   make test     # Run tests
   ```

## What's Included

The devcontainer includes all necessary tools and extensions:

### Build Tools & Libraries
- GCC/G++ with C++20 support
- CMake 3.20+ and Ninja
- JUCE dependencies (GTK3, ALSA, JACK, X11)
- pkg-config and build essentials

### Development Tools
- clang-format for code formatting
- clang-tidy for static analysis
- pre-commit hooks
- Python 3 with pip

### VS Code Extensions
- **clangd**: C++ IntelliSense and code navigation
- **CMake Tools**: CMake project management
- **GitHub Copilot**: AI pair programming assistant
- **GitLens**: Enhanced Git capabilities
- **Code Spell Checker**: Spell checking for code and comments

### Pre-configured Settings
- Format on save enabled
- clangd language server configured
- CMake build directory set to `./build`
- Ninja build system as default
- C++20 standard enabled

## Common Workflows

### Building the Project
```bash
# Debug build (recommended for development)
make debug

# Release build (optimized)
make release

# Clean build
make clean && make debug
```

### Running Tests
```bash
# Run all tests
make test

# Run tests with more verbosity
./build/tests/magda_tests -v
```

### Code Quality
```bash
# Format code
make format

# Run linter
make lint
```

### Working with Git
```bash
# The container mounts your .git folder
git status
git add .
git commit -m "Your message"
git push
```

## Troubleshooting

### Container Build Fails
**Problem**: Docker build fails with errors

**Solutions**:
- Ensure Docker Desktop is running
- Check you have at least 8GB disk space
- Try: Command Palette → `Dev Containers: Rebuild Container`
- Check Docker logs for specific errors

### clangd Not Working
**Problem**: No IntelliSense or code completion

**Solutions**:
1. Build the project first: `make debug`
2. Ensure `compile_commands.json` exists in `build/`
3. Reload VS Code: Command Palette → `Developer: Reload Window`
4. Check clangd output: View → Output → Select "Clangd"

### Slow Performance
**Problem**: Container is slow or unresponsive

**Solutions**:
- Allocate more CPU/RAM to Docker Desktop (Settings → Resources)
- For Codespaces: Use a larger machine type
- Check if background tasks are running (builds, indexing)

### Git Submodules Not Initialized
**Problem**: Missing Tracktion Engine or JUCE

**Solution**:
```bash
make setup
# Or manually:
git submodule update --init --recursive
```

### Build Errors
**Problem**: Compilation fails

**Solutions**:
1. Clean and rebuild: `make clean && make debug`
2. Check submodules are initialized: `git submodule status`
3. Verify you're using C++20: Check CMakeLists.txt
4. Look at the specific error message and fix accordingly

## Advanced Usage

### Customizing the Container

Edit `.devcontainer/devcontainer.json` to:
- Add more VS Code extensions
- Change build configurations
- Add environment variables
- Modify container run arguments

After changes, rebuild: Command Palette → `Dev Containers: Rebuild Container`

### Using Multiple Codespaces

You can create multiple codespaces for different branches:
- Main branch: stable development
- Feature branches: experimental work
- Each codespace is isolated

### Accessing Logs

**VS Code**:
- View → Output → Select appropriate channel
- Terminal → View logs for build output

**Codespaces**:
- Access via web interface
- Same as VS Code

### Port Forwarding

If you run a server (e.g., for debugging):
```bash
# The devcontainer can forward ports automatically
# Or manually forward: Ports panel → Forward a Port
```

## Comparison: Codespaces vs Local Dev Container

| Feature | GitHub Codespaces | VS Code Dev Container |
|---------|------------------|----------------------|
| **Setup Time** | Instant | Requires Docker install |
| **Cost** | Free tier available | Free (uses local resources) |
| **Performance** | Depends on machine type | Depends on local machine |
| **Internet Required** | Yes | Only for initial setup |
| **Storage** | Cloud | Local disk |
| **Best For** | Quick start, collaboration | Offline work, faster iteration |

## Getting Help

- **Devcontainer issues**: See [VS Code Docs](https://code.visualstudio.com/docs/devcontainers/containers)
- **Codespaces issues**: See [GitHub Docs](https://docs.github.com/en/codespaces)
- **Magda build issues**: Check [README.md](../README.md) and existing issues
- **General questions**: Open a GitHub discussion

## Next Steps

Once your environment is set up:
1. Read the [Architecture docs](architecture/)
2. Check the [Code Style Guide](code_style.md)
3. Review the [Testing Guide](testing-guide.md)
4. Start contributing! 🚀
