---
name: code-review
description: Comprehensive code review workflow for evaluating code quality, security, and best practices
license: MIT
---

# Code Review Skill

This skill provides a structured approach to reviewing code changes for quality, security, correctness, and adherence to best practices.

## When to Use This Skill

Use this skill when:
- Reviewing pull requests or code changes
- Performing security audits
- Evaluating code quality before merging
- Teaching or mentoring through code review

## Review Process

### 1. Understand the Context

Before reviewing code:
- Read the description of what the change is supposed to do
- Understand the business requirements or bug being fixed
- Review related issues or tickets
- Check if there are architectural specifications

### 2. Structural Review

Examine the overall structure:
- File organization and placement
- Module boundaries and dependencies
- Separation of concerns
- Design patterns used

### 3. Implementation Review

Examine the implementation details:
- Code clarity and readability
- Naming conventions (variables, functions, classes)
- Function/method length and complexity
- Appropriate use of language features
- Proper error handling

### 4. Security Review

Check for common vulnerabilities:
- **Injection Attacks**: SQL injection, command injection, XSS
- **Authentication/Authorization**: Proper access controls
- **Sensitive Data**: Credentials, keys, PII handling
- **Input Validation**: Untrusted input sanitization
- **Output Encoding**: Proper escaping for context
- **Cryptography**: Secure algorithms and key management
- **Dependencies**: Known vulnerable packages

### 5. Testing Review

Evaluate test coverage:
- Are critical paths tested?
- Are edge cases covered?
- Are error conditions tested?
- Are tests clear and maintainable?
- Is test coverage appropriate for risk level?

### 6. Performance and Efficiency

Consider performance implications:
- Algorithmic complexity
- Resource usage (memory, CPU, I/O)
- Database query efficiency
- Caching opportunities
- Network round-trips

### 7. Maintainability

Assess long-term maintainability:
- Code clarity and documentation
- Avoidance of technical debt
- Consistency with codebase patterns
- Ease of future modifications

## Review Checklist

Use `helpers/review-checklist.sh` to generate a customized checklist based on the files changed.

### Critical Issues (Must Fix Before Merge)
- [ ] Security vulnerabilities
- [ ] Data corruption or loss potential
- [ ] Breaking changes without migration path
- [ ] Correctness issues or logic errors
- [ ] Missing critical tests

### Important Issues (Should Fix Before Merge)
- [ ] Significant bugs or edge cases
- [ ] Poor error handling
- [ ] Performance problems
- [ ] Architectural inconsistencies
- [ ] Missing important tests

### Moderate Issues (Address or Document)
- [ ] Code clarity issues
- [ ] Missing or unclear documentation
- [ ] Inconsistent patterns
- [ ] Minor bugs
- [ ] Code duplication

### Minor Issues (Nice to Have)
- [ ] Style inconsistencies
- [ ] Optimization opportunities
- [ ] Refactoring suggestions
- [ ] Additional test scenarios

## Providing Feedback

### Structure Your Comments

```markdown
**[SEVERITY]**: Issue summary

**Problem**: Describe what's wrong and why it matters
**Location**: File path and line numbers
**Suggestion**: Specific recommendation for fixing
**Example**: Code example if helpful
```

### Severity Levels

- **CRITICAL**: Security, data loss, correctness - must fix
- **IMPORTANT**: Bugs, architecture, error handling - should fix
- **MODERATE**: Code quality, clarity, testing - address or document
- **MINOR**: Style, optimizations, suggestions - nice to have

### Be Constructive

- Focus on the code, not the person
- Explain the reasoning behind feedback
- Provide specific, actionable suggestions
- Acknowledge good practices
- Ask questions when intent is unclear

## Common Patterns to Watch For

### Anti-Patterns
- God objects (classes doing too much)
- Tight coupling between modules
- Unclear or misleading naming
- Swallowing exceptions
- Premature optimization
- Unnecessary abstraction

### Good Patterns
- Single Responsibility Principle
- Dependency injection
- Clear separation of concerns
- Explicit error handling
- Simple, readable code
- Appropriate abstraction level

## Using the Helper Scripts

### Generate Review Checklist
```bash
helpers/review-checklist.sh path/to/changed/files
```

### Run Security Scan
```bash
helpers/security-scan.sh
```

### Check Code Metrics
```bash
helpers/code-metrics.sh path/to/files
```

## Example Review

```markdown
## Summary
This PR implements user authentication with JWT tokens. Overall structure is good, but found some security concerns and test gaps.

## Critical Issues

**[CRITICAL]**: JWT secret stored in code

**Problem**: JWT secret is hardcoded in `auth.ts:45`. If this code is exposed, all tokens can be forged.
**Suggestion**: Store secret in environment variables or secrets manager.
**Example**:
\`\`\`typescript
const secret = process.env.JWT_SECRET;
if (!secret) throw new Error('JWT_SECRET not configured');
\`\`\`

## Important Issues

**[IMPORTANT]**: Missing password complexity validation

**Problem**: `validatePassword()` only checks length, not complexity (auth.ts:78)
**Suggestion**: Add checks for character variety, common passwords
**Impact**: Weak passwords can be easily braced

## Positive Observations

- Clean separation between authentication and authorization logic
- Good test coverage for happy path scenarios
- Clear error messages for users

## Recommendations

1. Fix critical security issues before merge
2. Add tests for authentication failure scenarios
3. Consider rate limiting for login attempts
```

## References

- [OWASP Top 10](https://owasp.org/www-project-top-ten/)
- [OWASP Code Review Guide](https://owasp.org/www-project-code-review-guide/)
- [Secure Coding Practices](https://owasp.org/www-project-secure-coding-practices-quick-reference-guide/)
