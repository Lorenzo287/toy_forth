#ifdef _WIN32
#define TEST_DEPENDENCY_EXPORT __declspec(dllexport)
#else
#define TEST_DEPENDENCY_EXPORT
#endif

TEST_DEPENDENCY_EXPORT int test_native_dependency_value(void) {
    return 42;
}
