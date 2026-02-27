---
name: documentation
description: Create clear, comprehensive technical documentation for code, APIs, and system architecture
license: MIT
---

# Documentation Skill

This skill provides guidance for creating effective technical documentation that helps developers understand and use code, APIs, and systems.

## When to Use This Skill

Use this skill when:
- Documenting APIs and interfaces
- Creating architecture documentation
- Writing technical specifications
- Documenting complex algorithms or business logic
- Creating onboarding materials

## Documentation Philosophy

### Good Documentation Is:

- **Clear**: Easy to understand for the target audience
- **Concise**: Says what's needed without verbosity
- **Current**: Kept up-to-date with code changes
- **Complete**: Covers necessary information
- **Discoverable**: Easy to find when needed

### Documentation Hierarchy

1. **Self-Documenting Code**: Clear naming, simple structure
2. **Inline Comments**: Complex logic, non-obvious decisions
3. **README Files**: Project overview, setup, usage
4. **API Documentation**: Endpoints, parameters, responses
5. **Architecture Docs**: System design, patterns, decisions
6. **Guides**: How-to guides, tutorials, examples

## Types of Documentation

### 1. Code Documentation

#### When to Comment

Add comments for:
- Complex algorithms or business logic
- Non-obvious decisions or trade-offs
- Workarounds for external issues
- Security considerations
- Performance optimizations

Don't comment:
- What the code does (make code self-explanatory)
- Obvious operations
- Every function (only complex ones)
- Redundant information

#### Good Comment Examples

```javascript
// Good: Explains WHY
// Using quadratic probing to reduce clustering in hash table
const index = (hash + i * i) % tableSize;

// Good: Explains non-obvious decision
// Timeout set to 30s because upstream API can take up to 25s
const TIMEOUT = 30000;

// Good: Explains workaround
// TODO: Remove when library bug #1234 is fixed
// Workaround: Call init twice due to library initialization bug
client.init();
client.init();

// Poor: Explains WHAT (obvious from code)
// Increment counter by 1
counter++;

// Poor: Redundant
// Get user by ID
function getUserById(id) { ... }
```

### 2. README Documentation

Every project should have a README with:

```markdown
# Project Name

Brief description of what the project does.

## Features

- Key feature 1
- Key feature 2
- Key feature 3

## Installation

```bash
npm install package-name
```

## Quick Start

```javascript
// Minimal example to get started
const lib = require('package-name');
const result = lib.doSomething();
```

## Usage

### Basic Usage
[Examples of common use cases]

### Advanced Usage
[Examples of advanced scenarios]

## Configuration

[Configuration options and environment variables]

## API Reference

[Link to detailed API docs]

## Development

### Setup
```bash
npm install
npm run dev
```

### Testing
```bash
npm test
```

### Building
```bash
npm run build
```

## Contributing

[Contribution guidelines]

## License

[License information]
```

### 3. API Documentation

Document each API endpoint with:

```markdown
### POST /api/users

Create a new user account.

**Authentication**: Required (Bearer token)

**Request Body**:
```json
{
  "email": "user@example.com",
  "password": "string (min 8 characters)",
  "name": "string",
  "role": "user|admin" (optional, default: "user")
}
```

**Response** (201 Created):
```json
{
  "id": "550e8400-e29b-41d4-a716-446655440000",
  "email": "user@example.com",
  "name": "John Doe",
  "role": "user",
  "createdAt": "2024-01-15T10:30:00Z"
}
```

**Errors**:
- `400 Bad Request`: Invalid input (e.g., email format, password too short)
- `401 Unauthorized`: Missing or invalid authentication token
- `409 Conflict`: Email already exists

**Example**:
```bash
curl -X POST https://api.example.com/api/users \
  -H "Authorization: Bearer YOUR_TOKEN" \
  -H "Content-Type: application/json" \
  -d '{"email":"user@example.com","password":"secure123","name":"John Doe"}'
```
```

### 4. Architecture Documentation

Document system architecture with:

#### Architecture Overview

```markdown
# System Architecture

## Overview

[High-level description of system purpose and approach]

## Architecture Diagram

[Diagram showing components and data flow]

## Components

### Frontend (React SPA)
- **Purpose**: User interface
- **Technology**: React, TypeScript, Redux
- **Responsibilities**: UI rendering, user interactions, state management

### API Gateway
- **Purpose**: Route requests, authentication
- **Technology**: Node.js, Express
- **Responsibilities**: Request routing, auth verification, rate limiting

### User Service
- **Purpose**: User management
- **Technology**: Node.js, PostgreSQL
- **Responsibilities**: User CRUD, authentication, authorization

### Message Queue
- **Purpose**: Async task processing
- **Technology**: RabbitMQ
- **Responsibilities**: Job queuing, event distribution

## Data Flow

1. User submits form in Frontend
2. Frontend sends POST to API Gateway
3. API Gateway validates auth token
4. API Gateway routes to User Service
5. User Service writes to Database
6. User Service publishes event to Message Queue
7. Background workers process event

## Security

- All external communication over HTTPS
- JWT tokens for authentication (30min expiry)
- API rate limiting: 100 req/min per user
- Input validation at gateway and service levels

## Scalability

- API Gateway: Horizontally scalable (stateless)
- Services: Horizontally scalable (stateless)
- Database: Primary-replica setup
- Message Queue: Clustered for HA

## Deployment

- Kubernetes cluster on AWS
- Services in separate namespaces
- Auto-scaling based on CPU/memory
- Rolling updates with zero downtime
```

### 5. Decision Documentation (ADRs)

Document significant decisions with Architecture Decision Records:

```markdown
# ADR-001: Use PostgreSQL for Primary Database

## Status
Accepted

## Context

We need to choose a database for storing user data, transactions, and application state. Requirements:
- ACID compliance for financial transactions
- Complex queries with joins
- Strong consistency
- Mature ecosystem

## Decision

Use PostgreSQL as primary database.

## Consequences

### Positive
- Strong ACID guarantees for transactions
- Excellent support for complex queries and joins
- Mature, battle-tested in production
- Rich ecosystem of tools and extensions
- Good performance for our scale

### Negative
- Vertical scaling limits (though sufficient for our needs)
- More complex operations than NoSQL for simple key-value access
- Requires careful index management for performance

### Neutral
- Team has PostgreSQL experience
- Managed PostgreSQL available on all major cloud providers

## Alternatives Considered

- **MongoDB**: Better for horizontal scaling, but lacks transactions
- **MySQL**: Similar capabilities, but less advanced features
- **DynamoDB**: Excellent scale, but vendor lock-in and complex pricing
```

## Documentation Tools

### Generate API Docs
```bash
helpers/generate-api-docs.sh
```

### Check Documentation Coverage
```bash
helpers/check-docs.sh
```

### Validate Links
```bash
helpers/validate-links.sh
```

## Documentation Best Practices

### Writing Style

- Use active voice
- Keep sentences short and clear
- Use examples liberally
- Define acronyms on first use
- Use consistent terminology

### Organization

- Start with overview/introduction
- Provide quick start guide
- Organize by task or topic
- Use clear headings and hierarchy
- Include table of contents for long docs

### Maintenance

- Review docs in code reviews
- Update docs with code changes
- Set up automated doc generation where possible
- Remove outdated documentation
- Version documentation with releases

### Examples

Always include:
- Minimal working example
- Common use cases
- Error handling examples
- Complete, runnable code

## Documentation Checklist

- [ ] README with setup and usage instructions
- [ ] API endpoints documented
- [ ] Configuration options explained
- [ ] Architecture diagram and description
- [ ] Security considerations documented
- [ ] Error handling explained
- [ ] Examples for common use cases
- [ ] Development setup instructions
- [ ] Testing instructions
- [ ] Deployment process documented

## Common Documentation Patterns

### Function Documentation

```javascript
/**
 * Calculate the total price including tax and shipping.
 *
 * @param {number} basePrice - Base price before additions
 * @param {number} taxRate - Tax rate as decimal (e.g., 0.08 for 8%)
 * @param {number} shippingCost - Flat shipping cost
 * @returns {number} Total price including all charges
 * @throws {Error} If basePrice or taxRate is negative
 *
 * @example
 * calculateTotal(100, 0.08, 10);  // Returns 118 (100 + 8 tax + 10 shipping)
 */
function calculateTotal(basePrice, taxRate, shippingCost) {
  // Implementation
}
```

### Class Documentation

```javascript
/**
 * User authentication service.
 *
 * Handles user login, logout, and token management.
 * Uses JWT tokens with 30-minute expiry.
 *
 * @example
 * const auth = new AuthService(secretKey);
 * const token = await auth.login('user@example.com', 'password');
 * const user = await auth.verifyToken(token);
 */
class AuthService {
  // Implementation
}
```

## References

- [Write the Docs](https://www.writethedocs.org/)
- [Google Developer Documentation Style Guide](https://developers.google.com/style)
- [Architecture Decision Records](https://adr.github.io/)
