# Security Policy

## Supported Versions

MAGDA is currently in early research and prototyping phase. Security updates will be provided for the following versions:

| Version | Supported          |
| ------- | ------------------ |
| main    | :white_check_mark: |
| develop | :white_check_mark: |
| < 1.0   | :x:                |

## Reporting a Vulnerability

We take security vulnerabilities seriously. If you discover a security issue in MAGDA, please report it responsibly.

### How to Report

**DO NOT** create a public GitHub issue for security vulnerabilities.

Instead, please report security vulnerabilities by:

1. **Email**: Send details to the repository maintainers via GitHub's private vulnerability reporting feature
2. **GitHub Security Advisory**: Use the "Security" tab → "Report a vulnerability" on the repository

### What to Include

When reporting a vulnerability, please include:

- **Description**: A clear description of the vulnerability
- **Impact**: What an attacker could achieve by exploiting this vulnerability
- **Reproduction Steps**: Detailed steps to reproduce the issue
- **Proof of Concept**: Code or commands that demonstrate the vulnerability
- **Affected Versions**: Which versions of MAGDA are affected
- **Suggested Fix**: If you have ideas on how to fix the issue (optional)

### What to Expect

- **Acknowledgment**: We will acknowledge receipt within 48 hours
- **Investigation**: We will investigate and validate the report within 7 days
- **Updates**: We will keep you informed about the progress of fixing the issue
- **Credit**: With your permission, we will credit you in the security advisory and release notes
- **Timeline**: We aim to release a fix within 30 days for critical vulnerabilities

## Security Best Practices

### For Contributors

When contributing to MAGDA, please:

1. **Never commit secrets**: No API keys, passwords, tokens, or credentials
2. **Validate input**: Always validate and sanitize user input
3. **Memory safety**: Use modern C++20 features and RAII to prevent memory issues
4. **Thread safety**: Ensure audio processing code is thread-safe and real-time safe
5. **Dependencies**: Keep dependencies up to date and review their security status
6. **Code review**: All PRs require review before merging to main
7. **CI checks**: All security checks must pass before merging

### For Users

When using MAGDA:

1. **Keep updated**: Always use the latest version from the main branch
2. **Trusted sources**: Only download MAGDA from the official GitHub repository
3. **Plugin safety**: Be cautious when loading third-party VST/AU plugins
4. **File validation**: Validate project files from untrusted sources
5. **Report issues**: Report any suspicious behavior or crashes

## Security Features

MAGDA implements several security measures:

### Automated Security Scanning

- **CodeQL Analysis**: Continuous scanning for security vulnerabilities in C++ code
- **Dependency Scanning**: Dependabot monitors for vulnerable dependencies
- **Secret Scanning**: Prevents accidental commit of secrets and credentials
- **Static Analysis**: clang-tidy checks for common security issues

### Build Security

- **Reproducible Builds**: CI ensures consistent, verifiable builds
- **Compiler Warnings**: Strict warnings enabled (-Wall -Wextra)
- **Sanitizers**: Address Sanitizer and Undefined Behavior Sanitizer in debug builds
- **Modern C++**: Using C++20 features for memory safety

### Branch Protection

The `main` branch is protected with:

- **Required Reviews**: Pull requests require approval before merging
- **Required CI**: All CI checks must pass before merging
- **Security Scans**: CodeQL and dependency checks must pass
- **No Force Push**: History cannot be rewritten
- **Signed Commits**: Encouraged for all commits

## Known Security Considerations

### Audio Engine Security

- **Plugin Sandboxing**: VST/AU plugins run in the same process (not sandboxed)
  - Only load plugins from trusted sources
  - Malicious plugins can access the full application memory space
  
- **File Parsing**: Project files are parsed using JUCE/Tracktion Engine
  - Malformed files could potentially cause crashes
  - Always validate files from untrusted sources

### Real-Time Audio Processing

- **Lock-Free Code**: Audio thread uses lock-free algorithms
  - Potential for race conditions if not properly implemented
  - All audio processing code is carefully reviewed

## Security Updates

Security updates will be announced through:

1. **GitHub Security Advisories**: Primary channel for security notifications
2. **Release Notes**: Security fixes highlighted in release notes
3. **README**: Critical security updates noted in the README

## Compliance

MAGDA follows:

- **OWASP Guidelines**: For general security best practices
- **CWE Top 25**: Mitigation of most dangerous software weaknesses
- **C++ Core Guidelines**: Memory safety and modern C++ practices
- **JUCE Best Practices**: Audio plugin security recommendations

## Security Contacts

- **Repository Maintainers**: Via GitHub private vulnerability reporting
- **Security Team**: See CODEOWNERS file for current maintainers

## Scope

This security policy covers:

- The MAGDA core application (C++ codebase)
- Build scripts and CI/CD pipeline
- Documentation and examples

This policy does NOT cover:

- Third-party dependencies (report to their respective maintainers)
- Third-party VST/AU plugins
- User-created project files or content

## Responsible Disclosure

We request that security researchers:

1. Give us reasonable time to fix vulnerabilities before public disclosure
2. Avoid accessing or modifying user data without permission
3. Do not perform attacks that could harm users or services
4. Do not publicly disclose vulnerabilities until fixed

We commit to:

1. Respond to vulnerability reports promptly
2. Keep reporters informed of progress
3. Credit researchers (with permission) in advisories
4. Release fixes in a timely manner

## Resources

- [GitHub Security Best Practices](https://docs.github.com/en/code-security)
- [OWASP Secure Coding Practices](https://owasp.org/www-project-secure-coding-practices-quick-reference-guide/)
- [C++ Core Guidelines - Security](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines)
- [JUCE Security Considerations](https://juce.com/discover/documentation)

## License

This security policy is part of the MAGDA project and is licensed under GPL-3.0.

---

**Last Updated**: 2026-01-30

For questions about this security policy, please open a discussion on GitHub or contact the maintainers.
