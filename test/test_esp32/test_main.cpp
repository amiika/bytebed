#include <Arduino.h>
#include <unity.h>
#include "vm.h"
#include "../tests.h"

// Set up globals
int32_t t_raw = 0;
PlayMode current_play_mode = MODE_BYTEBEAT;
int current_sample_rate = 8000;

void setUp(void) {
    // Called before every test
}

void tearDown(void) {
    // Called after every test
}

void test_bytebed_engine_on_hardware(void) {
    String testError = runBytebeatTestSuite();
    TEST_ASSERT_EQUAL_STRING("", testError.c_str());
}

void setup() {
    // Wait for the serial port to connect so the Mac doesn't miss the first few messages
    Serial.begin(115200);
    delay(2000); 

    UNITY_BEGIN();
    RUN_TEST(test_bytebed_engine_on_hardware);
    UNITY_END();
}

void loop() {
    // Unity tests run exactly once in setup(), so we do nothing here.
    delay(100);
}