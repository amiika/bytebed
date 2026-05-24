#include "vm.h"
#include "../tests.h"
#include <unity.h>

void setUp(void) {}
void tearDown(void) {}
void test_presets_integrity(void) {
    int failure_count = 0;
    String report = "";

    for (int b = 0; b < 10; b++) {
        for (int s = 0; s < 10; s++) {
            const PresetConfig& p = defaultBanks[b][s];
            if (strcmp(p.formula, "t") == 0 && p.sample_rate == 8000) continue;

            // 1. Infix Compile
            if (!compileInfix(String(p.formula), true)) {
                report += "B" + String(b) + "S" + String(s) + " | Infix Fail: " + last_vm_error + "\n";
                failure_count++;
                continue; // Do not abort, just log and move on
            }

            // 2. Round-Trip Decompile
            String rpn_str = decompile(true);
            
            // 3. RPN Compile
            if (!compileRPN(rpn_str)) {
                report += "B" + String(b) + "S" + String(s) + " | RPN Fail: " + last_vm_error + "\n-> RPN: " + rpn_str + "\n";
                failure_count++;
                continue;
            }

            // 4. Bytecode Validation
            if (!validateProgram(active_bank, prog_len_bank[active_bank])) {
                report += "BANK: " + String(b) + "PATCH: " + String(s) + " | Validation Fail: " + last_vm_error + "\n";
                failure_count++;
            }
        }
    }

    if (failure_count > 0) {
        printf("\nFailure report:\n%s\n", report.c_str());
        TEST_FAIL_MESSAGE("Integrity checks failed. See report above.");
    } else {
        printf("\nALL preset banks passed roundtrip!\n");
    }
}

void test_bytebed_engine(void) {
    String testError = runBytebeatTestSuite();
    TEST_ASSERT_EQUAL_STRING("", testError.c_str());
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_bytebed_engine);     
    RUN_TEST(test_presets_integrity);
    return UNITY_END();
}