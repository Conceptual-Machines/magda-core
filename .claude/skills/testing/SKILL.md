---
name: testing
description: Comprehensive testing strategy and implementation for creating reliable, maintainable test suites
license: MIT
---

# Testing Skill

This skill provides structured approaches to software testing, from unit tests to end-to-end tests, ensuring code quality and reliability.

## When to Use This Skill

Use this skill when:
- Creating tests for new features
- Adding test coverage to existing code
- Investigating test failures
- Refactoring tests for maintainability
- Establishing testing patterns

## Testing Philosophy

### Test Pyramid

```
       /\
      /e2e\      Few tests, test complete workflows
     /------\
    /integr.\   Moderate tests, test component interactions
   /----------\
  /   unit     \ Many tests, test individual functions
 /--------------\
```

- **Unit Tests**: Fast, focused, numerous
- **Integration Tests**: Moderate speed, test interactions
- **E2E Tests**: Slow, test complete user workflows

### Key Principles

1. **Test Behavior, Not Implementation**
   - Tests should survive refactoring
   - Focus on inputs, outputs, and side effects
   - Don't test private methods directly

2. **One Assertion Per Concept**
   - Each test verifies one specific behavior
   - Use multiple assertions for the same concept
   - Split unrelated assertions into separate tests

3. **Arrange-Act-Assert Pattern**
   ```
   // Arrange: Set up test data and conditions
   // Act: Execute the code under test
   // Assert: Verify the results
   ```

4. **Test Independence**
   - Tests should not depend on each other
   - Each test should set up its own state
   - Clean up after tests complete

## Testing Strategies by Level

### Unit Testing

Test individual functions and classes in isolation.

**What to Test:**
- Business logic and calculations
- Data transformations
- Validation logic
- Edge cases and boundary conditions
- Error handling

**What to Mock:**
- External services and APIs
- Database calls
- File system operations
- Time and randomness
- Network requests

**Example Structure:**
```javascript
describe('PasswordValidator', () => {
  describe('validateStrength', () => {
    it('should return weak for passwords under 8 characters', () => {
      // Arrange
      const validator = new PasswordValidator();
      const shortPassword = 'abc123';

      // Act
      const result = validator.validateStrength(shortPassword);

      // Assert
      expect(result.strength).toBe('weak');
      expect(result.reasons).toContain('too short');
    });
  });
});
```

### Integration Testing

Test interactions between components and modules.

**What to Test:**
- Component interactions
- Data flow between modules
- API endpoint behavior
- Database operations
- Message queue interactions

**Example Scenarios:**
- API request → Service → Database → Response
- Event publisher → Queue → Event handler → Side effect
- User action → Controller → Service → External API

**Example Structure:**
```javascript
describe('UserRegistration Integration', () => {
  it('should create user and send welcome email', async () => {
    // Arrange
    const userData = { email: 'test@example.com', password: 'secure123' };

    // Act
    const response = await request(app)
      .post('/api/users')
      .send(userData);

    // Assert
    expect(response.status).toBe(201);

    const user = await db.users.findByEmail(userData.email);
    expect(user).toBeDefined();

    const emailSent = await emailQueue.findByRecipient(userData.email);
    expect(emailSent.template).toBe('welcome');
  });
});
```

### End-to-End Testing

Test complete user workflows through the system.

**What to Test:**
- Critical user journeys
- Common workflows
- Multi-step processes
- Cross-system interactions

**Example Workflows:**
- User registration → Email verification → First login
- Add to cart → Checkout → Payment → Order confirmation
- Create document → Edit → Share → Collaborate

**Example Structure:**
```javascript
describe('E2E: Purchase Flow', () => {
  it('should complete full purchase journey', async () => {
    // User logs in
    await page.goto('/login');
    await page.fill('[name="email"]', 'user@example.com');
    await page.fill('[name="password"]', 'password123');
    await page.click('button[type="submit"]');

    // User adds product to cart
    await page.goto('/products/123');
    await page.click('button:has-text("Add to Cart")');

    // User completes checkout
    await page.goto('/checkout');
    await page.fill('[name="cardNumber"]', '4242424242424242');
    await page.click('button:has-text("Place Order")');

    // Verify order confirmation
    await expect(page.locator('.order-confirmation')).toBeVisible();
  });
});
```

## Test Coverage Strategy

### Coverage Goals

- **Critical Paths**: 100% coverage
- **Business Logic**: 90-100% coverage
- **Integration Points**: 80-90% coverage
- **UI Components**: 60-80% coverage
- **Utilities**: 80-90% coverage

Coverage is a guide, not a goal. Focus on testing behavior, not lines.

### What Deserves High Coverage

- Authentication and authorization
- Payment processing
- Data validation
- Security features
- Data transformations
- Critical business logic

### What Can Have Lower Coverage

- Simple getters/setters
- Framework configuration
- Trivial utilities
- Generated code
- Obvious wrapper functions

## Writing Good Tests

### Test Naming

Use descriptive names that explain what's being tested:

```javascript
// Good
it('should return 401 when user is not authenticated')
it('should send email when password reset is requested')
it('should calculate total price including tax and shipping')

// Poor
it('test login')
it('password works')
it('calculates correctly')
```

### Test Data

Use meaningful, realistic test data:

```javascript
// Good
const testUser = {
  email: 'john.doe@example.com',
  name: 'John Doe',
  age: 30
};

// Poor
const testUser = {
  email: 'a@b.c',
  name: 'x',
  age: 1
};
```

### Assertions

Use specific assertions with clear error messages:

```javascript
// Good
expect(response.status).toBe(200);
expect(response.body).toHaveProperty('userId');
expect(response.body.email).toBe('test@example.com');

// Poor
expect(response).toBeTruthy();
expect(response.body.userId).toBeDefined();
```

## Testing Edge Cases

Always test:
- **Boundary Values**: Min/max, empty, null, undefined
- **Invalid Input**: Wrong types, malformed data
- **Error Conditions**: Network failures, timeouts, exceptions
- **Concurrent Access**: Race conditions, locking
- **Resource Limits**: Large datasets, memory constraints

## Test Maintenance

### Keeping Tests Clean

- Remove obsolete tests
- Update tests when requirements change
- Refactor test code like production code
- Share test utilities and helpers
- Keep tests simple and focused

### Test Smells

Watch for:
- **Flaky Tests**: Non-deterministic failures
- **Slow Tests**: Taking too long to run
- **Coupled Tests**: Depending on execution order
- **Obscure Tests**: Unclear what's being tested
- **Complex Setup**: Too much arrangement code

## Using Helper Scripts

### Run Test Suite
```bash
helpers/run-tests.sh [unit|integration|e2e|all]
```

### Generate Coverage Report
```bash
helpers/coverage-report.sh
```

### Find Untested Code
```bash
helpers/find-untested.sh path/to/code
```

### Test Performance
```bash
helpers/test-performance.sh
```

## Common Testing Patterns

### Testing Async Code

```javascript
it('should fetch user data', async () => {
  const userId = '123';
  const user = await userService.getUser(userId);
  expect(user.id).toBe(userId);
});
```

### Testing Errors

```javascript
it('should throw error for invalid input', () => {
  expect(() => {
    validator.validate(null);
  }).toThrow('Input cannot be null');
});
```

### Testing Side Effects

```javascript
it('should call external service with correct params', async () => {
  const mockService = jest.fn();
  await handler.process({ service: mockService });

  expect(mockService).toHaveBeenCalledWith({
    action: 'process',
    timestamp: expect.any(Number)
  });
});
```

## Test Documentation

Document:
- Complex test scenarios
- Non-obvious test data
- Why certain things are mocked
- Expected behaviors being tested
- Known limitations or issues

## References

- [Testing Best Practices](https://testingjavascript.com/)
- [Test-Driven Development](https://martinfowler.com/bliki/TestDrivenDevelopment.html)
- [Unit Testing Principles](https://martinfowler.com/bliki/UnitTest.html)
