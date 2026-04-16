using System;

namespace LibiioTests
{
    public static class TestFramework
    {
        private static int totalTests = 0;
        private static int passedTests = 0;
        private static int failedTests = 0;

        public static void AssertTrue(bool condition, string message)
        {
            totalTests++;
            if (condition)
            {
                passedTests++;
                Console.WriteLine("  [PASS] " + message);
            }
            else
            {
                failedTests++;
                Console.WriteLine("  [FAIL] " + message);
            }
        }

        public static void AssertNotNull(object obj, string message)
        {
            AssertTrue(obj != null, message);
        }

        public static void AssertEqual<T>(T actual, T expected, string message)
        {
            AssertTrue(Equals(actual, expected),
                string.Format("{0} (expected: {1}, actual: {2})", message, expected, actual));
        }

        public static void AssertException<T>(Action action, string message) where T : Exception
        {
            totalTests++;
            try
            {
                action();
                failedTests++;
                Console.WriteLine("  [FAIL] " + message + " - No exception thrown");
            }
            catch (T)
            {
                passedTests++;
                Console.WriteLine("  [PASS] " + message);
            }
            catch (Exception ex)
            {
                failedTests++;
                Console.WriteLine("  [FAIL] " + message + " - Wrong exception type: " + ex.GetType().Name);
            }
        }

        public static void RunTest(string testName, Action testAction)
        {
            Console.WriteLine("\n[TEST] " + testName);
            try
            {
                testAction();
            }
            catch (Exception ex)
            {
                failedTests++;
                totalTests++;
                Console.WriteLine("  [FAIL] Unhandled exception: " + ex.Message);
                Console.WriteLine("         " + ex.StackTrace);
            }
        }

        public static int PrintSummary()
        {
            Console.WriteLine("\n========================================");
            Console.WriteLine("TEST SUMMARY");
            Console.WriteLine("========================================");
            Console.WriteLine("Total:  " + totalTests);
            Console.WriteLine("Passed: " + passedTests);
            Console.WriteLine("Failed: " + failedTests);

            if (totalTests > 0)
            {
                double successRate = (double)passedTests / totalTests * 100;
                Console.WriteLine("Success Rate: " + successRate.ToString("F1") + "%");
            }

            return failedTests > 0 ? 1 : 0;
        }
    }
}
