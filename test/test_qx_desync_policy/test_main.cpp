#include <unity.h>
#include <string.h>
#include "QxDesyncPolicy.h"

// US-141: QX_USB_ERR_DESYNC of the cypress 0665:5161 quirk, handled as the QX_USB_ERROR_DESYNC
// case of qx_command() (NUT PR #3720): a device reset up to 3 times in a row, then the end of
// the driver (here a board restart). Any command that returns >= 0 ends the streak.

typedef QxDesyncPolicy::Action A;

void setUp(void) {}
void tearDown(void) {}

void test_three_resets_then_restart(void) {
    QxDesyncPolicy p;
    TEST_ASSERT_TRUE(A::RESET_DEVICE == p.onResult(QX_USB_ERR_DESYNC));
    TEST_ASSERT_EQUAL_UINT8(1, p.resets());
    TEST_ASSERT_TRUE(A::RESET_DEVICE == p.onResult(QX_USB_ERR_DESYNC));
    TEST_ASSERT_TRUE(A::RESET_DEVICE == p.onResult(QX_USB_ERR_DESYNC));
    TEST_ASSERT_EQUAL_UINT8(3, p.resets());
    TEST_ASSERT_TRUE(A::RESTART == p.onResult(QX_USB_ERR_DESYNC));
}

void test_a_good_command_ends_the_streak(void) {
    QxDesyncPolicy p;
    p.onResult(QX_USB_ERR_DESYNC);
    p.onResult(QX_USB_ERR_DESYNC);
    TEST_ASSERT_TRUE(A::NONE == p.onResult(47));
    TEST_ASSERT_EQUAL_UINT8(0, p.resets());
    TEST_ASSERT_TRUE(A::RESET_DEVICE == p.onResult(QX_USB_ERR_DESYNC));
    TEST_ASSERT_EQUAL_UINT8(1, p.resets());
    // 0 (a timeout of NUT's usb_interrupt_read()) is >= 0 too
    TEST_ASSERT_TRUE(A::NONE == p.onResult(0));
    TEST_ASSERT_EQUAL_UINT8(0, p.resets());
}

// The other errors neither reset the device nor end the streak, as in qx_command()
void test_other_errors_leave_the_streak(void) {
    QxDesyncPolicy p;
    p.onResult(QX_USB_ERR_DESYNC);
    TEST_ASSERT_TRUE(A::NONE == p.onResult(QX_USB_ERR_TIMEOUT));
    TEST_ASSERT_TRUE(A::NONE == p.onResult(QX_USB_ERR_IO));
    TEST_ASSERT_TRUE(A::NONE == p.onResult(QX_USB_ERR_NO_DEVICE));
    TEST_ASSERT_EQUAL_UINT8(1, p.resets());
    TEST_ASSERT_TRUE(A::RESET_DEVICE == p.onResult(QX_USB_ERR_DESYNC));
    TEST_ASSERT_EQUAL_UINT8(2, p.resets());
}

void test_reason_fits_restart_record(void) {
    TEST_ASSERT_TRUE(strlen(QxDesyncPolicy::RESTART_REASON) <= 64);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_three_resets_then_restart);
    RUN_TEST(test_a_good_command_ends_the_streak);
    RUN_TEST(test_other_errors_leave_the_streak);
    RUN_TEST(test_reason_fits_restart_record);
    return UNITY_END();
}
