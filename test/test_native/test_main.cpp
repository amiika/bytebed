#include "vm.h"
#include "../tests.h"
#include <unity.h>

// Global mocks required by the VM (since we filtered out state.cpp)
int32_t t_raw = 0;
PlayMode current_play_mode = MODE_BYTEBEAT;
int current_sample_rate = 8000;

// Unity requires these two setup/teardown functions to exist, even if empty
void setUp(void) {}
void tearDown(void) {}

void test_bytebed_engine(void) {
    // Run your exact existing test suite
    String testError = runBytebeatTestSuite();
    
    // If it returns "", the test passes. 
    // If it returns an error string, Unity will print the exact formula that failed!
    TEST_ASSERT_EQUAL_STRING("", testError.c_str());
}

int main(void) {
    UNITY_BEGIN(); // Start the Unity framework
    
    RUN_TEST(test_bytebed_engine); // Register our engine test
    
    return UNITY_END(); // Close out and report
}