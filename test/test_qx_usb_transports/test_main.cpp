#include <unity.h>
#include <string.h>
#include <string>
#include <vector>
#include "QxUsbKrauler.h"
#include "FakeQxUsbPrimitives.h"
#include "QxTestingTables.h"

// US-137: the krauler family of the Q* USB subdrivers (krauler_command(), fabula_command(),
// hunnox_command() and hunnox_protocol() of nut_repo/drivers/nutdrv_qx.c, with
// usb_get_string_simple()) on fake primitives: string indices, langids, request lengths,
// retries, rebuilt answers, echo without transfers, errors, empty answers and the budget.

void setUp(void) {}
void tearDown(void) {}

namespace {

typedef FakeQxUsbPrimitives::StringCall Call;

const size_t BUF = 512; // QX_SMALLBUF of the engine

struct Run {
    int ret;
    std::string text; // the answer up to the first NUL
    uint32_t tookMs;
};

Run send(QxUsbTransport& t, FakeQxUsbPrimitives& usb, const char* cmd, char* buf = nullptr, size_t buflen = BUF) {
    static char local[BUF];
    if (!buf) {
        buf = local;
        buflen = sizeof(local);
    }
    memset(buf, 0, buflen);
    const uint32_t start = usb.clock;
    Run r;
    r.ret = t.command(cmd, strlen(cmd), buf, buflen);
    r.text = std::string(buf);
    r.tookMs = usb.clock - start;
    return r;
}

void assertCall(const Call& c, uint8_t index, uint16_t langid, size_t buflen) {
    TEST_ASSERT_EQUAL_HEX8(index, c.index);
    TEST_ASSERT_EQUAL_HEX16(langid, c.langid);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)buflen, (uint32_t)c.buflen);
}

// The two reads of usb_get_string_simple() for index, starting at stringCalls[from]
void assertSimpleRead(const FakeQxUsbPrimitives& usb, size_t from, uint8_t index, uint16_t langid = 0x0409) {
    TEST_ASSERT_TRUE(usb.stringCalls.size() >= from + 2);
    assertCall(usb.stringCalls[from], 0x00, 0x0000, 255);
    assertCall(usb.stringCalls[from + 1], index, langid, 255);
}

} // namespace

// --- qxGetStringAscii() ---

void test_ascii_reads_the_langid_list_then_the_string(void) {
    FakeQxUsbPrimitives usb(MEGATEC_TESTING);
    usb.stringDevice = true;
    char buf[BUF] = {0};
    int ret = qxGetStringAscii(usb, 0x03, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("(215.0 195.0 230.0 014 49.0 22.7 30.0 00000000\r", buf);
    TEST_ASSERT_EQUAL((int)strlen(buf), ret);
    TEST_ASSERT_EQUAL(2, (int)usb.stringCalls.size());
    assertSimpleRead(usb, 0, 0x03);
}

void test_ascii_uses_the_first_langid_of_the_device(void) {
    FakeQxUsbPrimitives usb(MEGATEC_TESTING);
    usb.stringDevice = true;
    usb.langidList = 0x0407;
    char buf[BUF] = {0};
    TEST_ASSERT_TRUE(qxGetStringAscii(usb, 0x0d, buf, sizeof(buf)) > 0);
    assertSimpleRead(usb, 0, 0x0d, 0x0407);
}

void test_ascii_replaces_non_ascii_characters(void) {
    FakeQxUsbPrimitives usb(nullptr);
    usb.script(FakeQxUsbPrimitives::langidDescriptor(0x0409));
    // 'A', U+00E9 (bit 7 of the low byte), U+0142 (high byte), 'Z'
    usb.script(std::vector<uint8_t>{10, 0x03, 'A', 0, 0xE9, 0, 0x42, 0x01, 'Z', 0});
    char buf[BUF] = {0};
    TEST_ASSERT_EQUAL(4, qxGetStringAscii(usb, 0x03, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("A??Z", buf);
}

void test_ascii_truncates_to_buflen_minus_one(void) {
    FakeQxUsbPrimitives usb(nullptr);
    usb.scriptAnswer("ABCDEF");
    char buf[8];
    memset(buf, 'x', sizeof(buf));
    TEST_ASSERT_EQUAL(3, qxGetStringAscii(usb, 0x03, buf, 4));
    TEST_ASSERT_EQUAL_STRING("ABC", buf);
    TEST_ASSERT_EQUAL('x', buf[4]);
}

void test_ascii_index_zero_is_refused_without_traffic(void) {
    FakeQxUsbPrimitives usb(nullptr);
    usb.stringDevice = true;
    char buf[BUF] = {0};
    TEST_ASSERT_EQUAL(QX_USB_ERR_INVALID_PARAM, qxGetStringAscii(usb, 0x00, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL(0, (int)usb.stringCalls.size());
}

void test_ascii_malformed_descriptors_are_io_errors(void) {
    char buf[BUF] = {0};
    {
        // Langid list shorter than 4 bytes: the string is not asked
        FakeQxUsbPrimitives usb(nullptr);
        usb.script(std::vector<uint8_t>{2, 0x03});
        TEST_ASSERT_EQUAL(QX_USB_ERR_IO, qxGetStringAscii(usb, 0x03, buf, sizeof(buf)));
        TEST_ASSERT_EQUAL(1, (int)usb.stringCalls.size());
    }
    {
        // Not a string descriptor
        FakeQxUsbPrimitives usb(nullptr);
        usb.script(FakeQxUsbPrimitives::langidDescriptor(0x0409));
        usb.script(std::vector<uint8_t>{4, 0x02, 'A', 0});
        TEST_ASSERT_EQUAL(QX_USB_ERR_IO, qxGetStringAscii(usb, 0x03, buf, sizeof(buf)));
    }
    {
        // bLength beyond the bytes received
        FakeQxUsbPrimitives usb(nullptr);
        usb.script(FakeQxUsbPrimitives::langidDescriptor(0x0409));
        usb.script(std::vector<uint8_t>{10, 0x03, 'A', 0});
        TEST_ASSERT_EQUAL(QX_USB_ERR_IO, qxGetStringAscii(usb, 0x03, buf, sizeof(buf)));
    }
    {
        // Nothing received for the string
        FakeQxUsbPrimitives usb(nullptr);
        usb.script(FakeQxUsbPrimitives::langidDescriptor(0x0409));
        usb.script(std::vector<uint8_t>{});
        TEST_ASSERT_EQUAL(QX_USB_ERR_IO, qxGetStringAscii(usb, 0x03, buf, sizeof(buf)));
    }
}

void test_ascii_errors_of_the_primitive_are_returned(void) {
    char buf[BUF] = {0};
    {
        FakeQxUsbPrimitives usb(nullptr);
        usb.scriptError(QX_USB_ERR_TIMEOUT);
        TEST_ASSERT_EQUAL(QX_USB_ERR_TIMEOUT, qxGetStringAscii(usb, 0x03, buf, sizeof(buf)));
        TEST_ASSERT_EQUAL(1, (int)usb.stringCalls.size());
    }
    {
        FakeQxUsbPrimitives usb(nullptr);
        usb.script(FakeQxUsbPrimitives::langidDescriptor(0x0409));
        usb.scriptError(QX_USB_ERR_PIPE);
        TEST_ASSERT_EQUAL(QX_USB_ERR_PIPE, qxGetStringAscii(usb, 0x03, buf, sizeof(buf)));
        TEST_ASSERT_EQUAL(2, (int)usb.stringCalls.size());
    }
}

void test_ascii_empty_string_is_zero(void) {
    FakeQxUsbPrimitives usb(nullptr);
    usb.scriptAnswer("");
    char buf[BUF];
    memset(buf, 'x', sizeof(buf));
    TEST_ASSERT_EQUAL(0, qxGetStringAscii(usb, 0x03, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_STRING("", buf);
}

// --- krauler ---

void test_krauler_indices_and_prefixes(void) {
    struct Case { const char* cmd; uint8_t index; char prefix; };
    const Case cases[] = {
        { "Q1\r", 0x03, '(' }, { "F\r", 0x0d, '#' }, { "I\r", 0x0c, '#' }, { "T\r", 0x04, '\r' },
        { "TL\r", 0x05, '\r' }, { "Q\r", 0x07, '\r' }, { "C\r", 0x0b, '\r' }, { "CT\r", 0x0b, '\r' },
    };
    for (const Case& c : cases) {
        FakeQxUsbPrimitives usb(nullptr);
        QxKraulerTransport t(usb);
        usb.scriptAnswer("?ANSWER\r");
        Run r = send(t, usb, c.cmd);
        TEST_ASSERT_EQUAL_MESSAGE(8, r.ret, c.cmd);
        TEST_ASSERT_EQUAL_MESSAGE(c.prefix, r.text[0], c.cmd);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("ANSWER\r", r.text.c_str() + 1, c.cmd);
        TEST_ASSERT_EQUAL_MESSAGE(2, (int)usb.stringCalls.size(), c.cmd);
        assertSimpleRead(usb, 0, c.index);
    }
}

void test_krauler_adds_the_missing_cr(void) {
    FakeQxUsbPrimitives usb(nullptr);
    QxKraulerTransport t(usb);
    usb.scriptAnswer("x230.0 000 024.0 50.0");
    Run r = send(t, usb, "F\r");
    TEST_ASSERT_EQUAL_STRING("#230.0 000 024.0 50.0\r", r.text.c_str());
    TEST_ASSERT_EQUAL(22, r.ret);
}

void test_krauler_no_cr_added_without_room(void) {
    FakeQxUsbPrimitives usb(nullptr);
    QxKraulerTransport t(usb);
    usb.scriptAnswer("xABCDEFGH");
    char buf[8];
    // 7 characters fit (buflen - 1), ret == buflen - 1: no room for the CR
    Run r = send(t, usb, "Q1\r", buf, sizeof(buf));
    TEST_ASSERT_EQUAL(7, r.ret);
    TEST_ASSERT_EQUAL_STRING("(ABCDEF", buf);
}

void test_krauler_retries_on_ups_no_ack(void) {
    FakeQxUsbPrimitives usb(nullptr);
    QxKraulerTransport t(usb);
    for (int i = 0; i < 3; i++) usb.scriptAnswer("UPS No Ack");
    usb.scriptAnswer("(215.0\r");
    Run r = send(t, usb, "Q1\r");
    TEST_ASSERT_EQUAL_STRING("(215.0\r", r.text.c_str());
    TEST_ASSERT_EQUAL(8, (int)usb.stringCalls.size());
    for (size_t i = 0; i < 8; i += 2) assertSimpleRead(usb, i, 0x03);
}

void test_krauler_gives_up_after_ten_no_ack(void) {
    FakeQxUsbPrimitives usb(nullptr);
    QxKraulerTransport t(usb);
    for (int i = 0; i < 11; i++) usb.scriptAnswer("ups no ack\r");
    Run r = send(t, usb, "Q1\r");
    TEST_ASSERT_EQUAL(0, r.ret);
    TEST_ASSERT_EQUAL(20, (int)usb.stringCalls.size());
}

void test_krauler_empty_answer_and_errors_end_at_the_first_attempt(void) {
    {
        FakeQxUsbPrimitives usb(nullptr);
        QxKraulerTransport t(usb);
        usb.scriptAnswer("");
        usb.scriptAnswer("(215.0\r"); // not reached
        TEST_ASSERT_EQUAL(0, send(t, usb, "Q1\r").ret);
        TEST_ASSERT_EQUAL(2, (int)usb.stringCalls.size());
    }
    {
        FakeQxUsbPrimitives usb(nullptr);
        QxKraulerTransport t(usb);
        usb.script(FakeQxUsbPrimitives::langidDescriptor(0x0409));
        usb.scriptError(QX_USB_ERR_PIPE);
        TEST_ASSERT_EQUAL(QX_USB_ERR_PIPE, send(t, usb, "Q1\r").ret);
        TEST_ASSERT_EQUAL(2, (int)usb.stringCalls.size());
    }
    {
        FakeQxUsbPrimitives usb(nullptr);
        QxKraulerTransport t(usb);
        usb.scriptError(QX_USB_ERR_TIMEOUT);
        TEST_ASSERT_EQUAL(QX_USB_ERR_TIMEOUT, send(t, usb, "Q1\r").ret);
        TEST_ASSERT_EQUAL(1, (int)usb.stringCalls.size());
    }
}

void test_krauler_unknown_command_is_echoed_without_transfers(void) {
    const char* cmds[] = { "QS\r", "T05\r", "S03\r", "FW?\r" };
    for (const char* c : cmds) {
        FakeQxUsbPrimitives usb(nullptr);
        usb.stringDevice = true;
        QxKraulerTransport t(usb);
        Run r = send(t, usb, c);
        TEST_ASSERT_EQUAL_MESSAGE((int)strlen(c), r.ret, c);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(c, r.text.c_str(), c);
        TEST_ASSERT_EQUAL_MESSAGE(0, (int)usb.stringCalls.size(), c);
        TEST_ASSERT_EQUAL_MESSAGE(0u, r.tookMs, c);
    }
}

void test_krauler_retries_stop_at_the_budget(void) {
    FakeQxUsbPrimitives usb(MEGATEC_TESTING);
    usb.stringDevice = true;
    usb.stringTransferMs = 400; // 10 attempts x 2 reads would take 8 s
    usb.device.override("Q1\r", "UPS No Ack\r");
    QxKraulerTransport t(usb);
    Run r = send(t, usb, "Q1\r");
    TEST_ASSERT_EQUAL(QX_USB_ERR_TIMEOUT, r.ret);
    TEST_ASSERT_TRUE(r.tookMs <= QxKraulerTransport::BUDGET_MS + 400);
    TEST_ASSERT_TRUE(usb.stringCalls.size() < 20);
}

void test_krauler_mute_ups_stays_within_the_budget(void) {
    FakeQxUsbPrimitives usb(MEGATEC_TESTING);
    usb.stringDevice = true;
    usb.muted = true;
    QxKraulerTransport t(usb);
    Run r = send(t, usb, "Q1\r");
    TEST_ASSERT_TRUE(r.ret < 0);
    TEST_ASSERT_TRUE(r.tookMs <= QxKraulerTransport::BUDGET_MS + QX_CTRL_TIMEOUT_MS);
}

// --- fabula ---

// The index asked by cmd (second read of usb_get_string_simple()), 0 when nothing was read
uint8_t fabulaIndexOf(const char* cmd) {
    FakeQxUsbPrimitives usb(MEGATEC_TESTING);
    usb.stringDevice = true;
    QxFabulaTransport t(usb);
    send(t, usb, cmd);
    return usb.stringCalls.size() >= 2 ? usb.stringCalls[1].index : 0;
}

void test_fabula_command_indices(void) {
    TEST_ASSERT_EQUAL_HEX8(0x03, fabulaIndexOf("Q1\r"));
    TEST_ASSERT_EQUAL_HEX8(0x0d, fabulaIndexOf("F\r"));
    TEST_ASSERT_EQUAL_HEX8(0x0c, fabulaIndexOf("I\r"));
    TEST_ASSERT_EQUAL_HEX8(0x07, fabulaIndexOf("Q\r"));
    TEST_ASSERT_EQUAL_HEX8(0x0a, fabulaIndexOf("C\r"));
}

void test_fabula_shutdown_encoding(void) {
    TEST_ASSERT_EQUAL_HEX8(0x10, fabulaIndexOf("S.5R0000\r"));
    TEST_ASSERT_EQUAL_HEX8(0x30, fabulaIndexOf("S.7R0000\r"));
    TEST_ASSERT_EQUAL_HEX8(0x68, fabulaIndexOf("S01R0001\r"));
    TEST_ASSERT_EQUAL_HEX8(0x88, fabulaIndexOf("S03R0002\r"));
    TEST_ASSERT_EQUAL_HEX8(0xF0, fabulaIndexOf("S10R0000\r"));
    // "S<n>\r": val2 stays -1, so shutdown.return (+8); delay at least .5
    TEST_ASSERT_EQUAL_HEX8(0x18, fabulaIndexOf("S.3\r"));
    // delay at most 10
    TEST_ASSERT_EQUAL_HEX8(0xF0, fabulaIndexOf("S20R0000\r"));
}

void test_fabula_answers(void) {
    {
        // Missing CR added
        FakeQxUsbPrimitives usb(nullptr);
        QxFabulaTransport t(usb);
        usb.scriptAnswer("(215.0");
        Run r = send(t, usb, "Q1\r");
        TEST_ASSERT_EQUAL(7, r.ret);
        TEST_ASSERT_EQUAL_STRING("(215.0\r", r.text.c_str());
    }
    {
        // "UPS No Ack": 0 with the whole buffer zeroed, no retry
        FakeQxUsbPrimitives usb(nullptr);
        QxFabulaTransport t(usb);
        usb.scriptAnswer("UPS No Ack\r");
        char buf[64];
        memset(buf, 'x', sizeof(buf));
        TEST_ASSERT_EQUAL(0, t.command("Q\r", 2, buf, sizeof(buf)));
        for (size_t i = 0; i < sizeof(buf); i++) TEST_ASSERT_EQUAL_HEX8(0, buf[i]);
        TEST_ASSERT_EQUAL(2, (int)usb.stringCalls.size());
    }
    {
        FakeQxUsbPrimitives usb(nullptr);
        QxFabulaTransport t(usb);
        usb.scriptAnswer("");
        TEST_ASSERT_EQUAL(0, send(t, usb, "Q1\r").ret);
    }
    {
        FakeQxUsbPrimitives usb(nullptr);
        QxFabulaTransport t(usb);
        usb.scriptError(QX_USB_ERR_TIMEOUT);
        TEST_ASSERT_EQUAL(QX_USB_ERR_TIMEOUT, send(t, usb, "Q1\r").ret);
    }
    {
        // Longest string descriptor (126 characters) and the CR
        FakeQxUsbPrimitives usb(MEGATEC_TESTING);
        usb.stringDevice = true;
        usb.device.override("Q1\r", std::string(150, 'A').c_str());
        QxFabulaTransport t(usb);
        TEST_ASSERT_EQUAL(127, send(t, usb, "Q1\r").ret);
    }
}

void test_fabula_unknown_command_is_echoed_without_transfers(void) {
    const char* cmds[] = { "T\r", "TL\r", "BL\r", "QS\r" };
    for (const char* c : cmds) {
        FakeQxUsbPrimitives usb(nullptr);
        usb.stringDevice = true;
        QxFabulaTransport t(usb);
        Run r = send(t, usb, c);
        TEST_ASSERT_EQUAL_MESSAGE((int)strlen(c), r.ret, c);
        TEST_ASSERT_EQUAL_STRING_MESSAGE(c, r.text.c_str(), c);
        TEST_ASSERT_EQUAL_MESSAGE(0, (int)usb.stringCalls.size(), c);
    }
}

void test_fabula_mute_ups_stays_within_the_budget(void) {
    FakeQxUsbPrimitives usb(MEGATEC_TESTING);
    usb.stringDevice = true;
    usb.muted = true;
    QxFabulaTransport t(usb);
    Run r = send(t, usb, "Q1\r");
    TEST_ASSERT_TRUE(r.ret < 0);
    TEST_ASSERT_TRUE(r.tookMs <= QxFabulaTransport::BUDGET_MS + QX_CTRL_TIMEOUT_MS);
}

// --- hunnox ---

void test_hunnox_preamble_lock_step_over_five_commands(void) {
    FakeQxUsbPrimitives usb(MEGATEC_TESTING);
    usb.stringDevice = true;
    QxHunnoxTransport t(usb);
    const uint16_t L = QxHunnoxTransport::PREAMBLE_LANGID;
    TEST_ASSERT_EQUAL_HEX16(0x0409, L);

    // 1: step 0, strings 0, 0, 1 with 1026 bytes, then 10 ms
    Run r = send(t, usb, "Q1\r");
    TEST_ASSERT_EQUAL_STRING("(215.0 195.0 230.0 014 49.0 22.7 30.0 00000000\r", r.text.c_str());
    TEST_ASSERT_EQUAL(5, (int)usb.stringCalls.size());
    assertCall(usb.stringCalls[0], 0x00, L, 1026);
    assertCall(usb.stringCalls[1], 0x00, L, 1026);
    assertCall(usb.stringCalls[2], 0x01, L, 1026);
    assertSimpleRead(usb, 3, 0x03);
    TEST_ASSERT_EQUAL(1, (int)usb.sleeps.size());
    TEST_ASSERT_EQUAL_UINT32(10, usb.sleeps[0]);
    // the 10 ms are between the preamble and the command
    TEST_ASSERT_TRUE(usb.stringCalls[3].atMs - usb.stringCalls[2].atMs >= 10);

    // 2: step 1, 0x0d with 102 bytes
    usb.stringCalls.clear();
    send(t, usb, "Q1\r");
    TEST_ASSERT_EQUAL(3, (int)usb.stringCalls.size());
    assertCall(usb.stringCalls[0], 0x0d, L, 102);
    assertSimpleRead(usb, 1, 0x03);

    // 3: step 2, 0x03 is the command itself: nothing more
    usb.stringCalls.clear();
    send(t, usb, "Q1\r");
    TEST_ASSERT_EQUAL(2, (int)usb.stringCalls.size());
    assertSimpleRead(usb, 0, 0x03);

    // 4: step 3, 0x0c
    usb.stringCalls.clear();
    send(t, usb, "Q1\r");
    TEST_ASSERT_EQUAL(3, (int)usb.stringCalls.size());
    assertCall(usb.stringCalls[0], 0x0c, L, 102);

    // 5: back to step 1
    usb.stringCalls.clear();
    send(t, usb, "Q1\r");
    TEST_ASSERT_EQUAL(3, (int)usb.stringCalls.size());
    assertCall(usb.stringCalls[0], 0x0d, L, 102);
    TEST_ASSERT_EQUAL(1, (int)usb.sleeps.size()); // only the step 0 waits
}

void test_hunnox_preamble_skips_the_index_being_asked(void) {
    FakeQxUsbPrimitives usb(MEGATEC_TESTING);
    usb.stringDevice = true;
    QxHunnoxTransport t(usb);
    send(t, usb, "Q1\r"); // step 0
    usb.stringCalls.clear();
    send(t, usb, "F\r"); // step 1 with 0x0d itself
    TEST_ASSERT_EQUAL(2, (int)usb.stringCalls.size());
    assertSimpleRead(usb, 0, 0x0d);
    usb.stringCalls.clear();
    send(t, usb, "F\r"); // step 2: 0x03
    assertCall(usb.stringCalls[0], 0x03, 0x0409, 102);
    usb.stringCalls.clear();
    send(t, usb, "I\r"); // step 3 with 0x0c itself
    TEST_ASSERT_EQUAL(2, (int)usb.stringCalls.size());
    assertSimpleRead(usb, 0, 0x0c);
}

// US-138: with novendor step 3 of the preamble reads nothing (hunnox_protocol(),
// nutdrv_qx.c:1276); the other steps are those of the lock-step above
void test_hunnox_novendor_skips_0x0c_in_the_preamble(void) {
    FakeQxUsbPrimitives usb(MEGATEC_TESTING);
    usb.stringDevice = true;
    QxHunnoxTransport t(usb);
    QxOptions opt;
    opt.novendor = true;
    t.setOptions(opt);
    const uint16_t L = QxHunnoxTransport::PREAMBLE_LANGID;

    send(t, usb, "Q1\r"); // step 0: 0, 0, 1
    TEST_ASSERT_EQUAL(5, (int)usb.stringCalls.size());
    usb.stringCalls.clear();
    send(t, usb, "Q1\r"); // step 1: 0x0d
    TEST_ASSERT_EQUAL(3, (int)usb.stringCalls.size());
    assertCall(usb.stringCalls[0], 0x0d, L, 102);
    usb.stringCalls.clear();
    send(t, usb, "Q1\r"); // step 2: 0x03 is the command itself
    TEST_ASSERT_EQUAL(2, (int)usb.stringCalls.size());
    usb.stringCalls.clear();
    Run r = send(t, usb, "Q1\r"); // step 3: no 0x0c, only the command
    TEST_ASSERT_EQUAL(2, (int)usb.stringCalls.size());
    assertSimpleRead(usb, 0, 0x03);
    TEST_ASSERT_EQUAL_STRING("(215.0 195.0 230.0 014 49.0 22.7 30.0 00000000\r", r.text.c_str());
    for (const auto& c : usb.stringCalls) TEST_ASSERT_NOT_EQUAL(0x0c, c.index);
    usb.stringCalls.clear();
    send(t, usb, "Q1\r"); // back to step 1
    assertCall(usb.stringCalls[0], 0x0d, L, 102);
}

// Without novendor (or with it off again) step 3 reads 0x0c as in US-137
void test_hunnox_options_default_reads_0x0c(void) {
    FakeQxUsbPrimitives usb(MEGATEC_TESTING);
    usb.stringDevice = true;
    QxHunnoxTransport t(usb);
    t.setOptions(QxOptions());
    for (int i = 0; i < 3; i++) send(t, usb, "Q1\r");
    usb.stringCalls.clear();
    send(t, usb, "Q1\r");
    TEST_ASSERT_EQUAL(3, (int)usb.stringCalls.size());
    assertCall(usb.stringCalls[0], 0x0c, QxHunnoxTransport::PREAMBLE_LANGID, 102);
}

void test_hunnox_battery_charge_index(void) {
    FakeQxUsbPrimitives usb(MEGATEC_TESTING);
    usb.stringDevice = true;
    QxHunnoxTransport t(usb);
    send(t, usb, "BL\r");
    TEST_ASSERT_EQUAL(5, (int)usb.stringCalls.size());
    assertSimpleRead(usb, 3, 0xf3);
}

void test_hunnox_unknown_command_is_echoed_before_the_preamble(void) {
    FakeQxUsbPrimitives usb(MEGATEC_TESTING);
    usb.stringDevice = true;
    QxHunnoxTransport t(usb);
    Run r = send(t, usb, "FW?\r");
    TEST_ASSERT_EQUAL(4, r.ret);
    TEST_ASSERT_EQUAL_STRING("FW?\r", r.text.c_str());
    TEST_ASSERT_EQUAL(0, (int)usb.stringCalls.size());
    TEST_ASSERT_EQUAL(0, (int)usb.sleeps.size());
    // The step did not advance: the next command still has the step 0 preamble
    send(t, usb, "Q1\r");
    TEST_ASSERT_EQUAL(5, (int)usb.stringCalls.size());
    assertCall(usb.stringCalls[2], 0x01, 0x0409, 1026);
}

void test_hunnox_shutdown_encoding(void) {
    FakeQxUsbPrimitives usb(MEGATEC_TESTING);
    usb.stringDevice = true;
    QxHunnoxTransport t(usb);
    send(t, usb, "S01R0001\r");
    assertCall(usb.stringCalls[4], 0x68, 0x0409, 255);
}

void test_hunnox_answer_is_at_most_101_characters_without_added_cr(void) {
    {
        FakeQxUsbPrimitives usb(MEGATEC_TESTING);
        usb.stringDevice = true;
        usb.device.override("Q1\r", std::string(126, 'A').c_str());
        QxHunnoxTransport t(usb);
        Run r = send(t, usb, "Q1\r");
        TEST_ASSERT_EQUAL(101, r.ret);
        TEST_ASSERT_EQUAL(101, (int)r.text.size());
        // usb_get_string_simple() still asks 255 bytes
        assertCall(usb.stringCalls[4], 0x03, 0x0409, 255);
    }
    {
        FakeQxUsbPrimitives usb(MEGATEC_TESTING);
        usb.stringDevice = true;
        usb.device.override("Q1\r", "(215.0");
        QxHunnoxTransport t(usb);
        Run r = send(t, usb, "Q1\r");
        TEST_ASSERT_EQUAL(6, r.ret);
        TEST_ASSERT_EQUAL_STRING("(215.0", r.text.c_str());
    }
}

void test_hunnox_no_ack_zeroes_102_bytes(void) {
    FakeQxUsbPrimitives usb(MEGATEC_TESTING);
    usb.stringDevice = true;
    usb.device.override("Q\r", "UPS No Ack\r");
    QxHunnoxTransport t(usb);
    char buf[200];
    memset(buf, 'x', sizeof(buf));
    TEST_ASSERT_EQUAL(0, t.command("Q\r", 2, buf, sizeof(buf)));
    for (size_t i = 0; i < 102; i++) TEST_ASSERT_EQUAL_HEX8(0, buf[i]);
    TEST_ASSERT_EQUAL('x', buf[102]);
}

void test_hunnox_preamble_errors_are_ignored(void) {
    FakeQxUsbPrimitives usb(nullptr);
    QxHunnoxTransport t(usb);
    usb.scriptError(QX_USB_ERR_PIPE);
    usb.scriptError(QX_USB_ERR_PIPE);
    usb.scriptError(QX_USB_ERR_PIPE);
    usb.scriptAnswer("(215.0\r");
    Run r = send(t, usb, "Q1\r");
    TEST_ASSERT_EQUAL(7, r.ret);
    TEST_ASSERT_EQUAL_STRING("(215.0\r", r.text.c_str());
}

void test_hunnox_empty_answer_and_errors(void) {
    {
        FakeQxUsbPrimitives usb(MEGATEC_TESTING);
        usb.stringDevice = true;
        usb.device.override("Q1\r", "");
        QxHunnoxTransport t(usb);
        TEST_ASSERT_EQUAL(0, send(t, usb, "Q1\r").ret);
    }
    {
        FakeQxUsbPrimitives usb(nullptr);
        QxHunnoxTransport t(usb);
        for (int i = 0; i < 3; i++) usb.script(FakeQxUsbPrimitives::langidDescriptor(0x0409));
        usb.scriptError(QX_USB_ERR_TIMEOUT);
        TEST_ASSERT_EQUAL(QX_USB_ERR_TIMEOUT, send(t, usb, "Q1\r").ret);
    }
}

void test_hunnox_mute_ups_stays_within_the_budget(void) {
    FakeQxUsbPrimitives usb(MEGATEC_TESTING);
    usb.stringDevice = true;
    usb.muted = true;
    QxHunnoxTransport t(usb);
    Run r = send(t, usb, "Q1\r");
    TEST_ASSERT_TRUE(r.ret < 0);
    TEST_ASSERT_TRUE(r.tookMs <= QxHunnoxTransport::BUDGET_MS + QX_CTRL_TIMEOUT_MS);
    // A later command too (preamble of one read)
    r = send(t, usb, "Q1\r");
    TEST_ASSERT_TRUE(r.ret < 0);
    TEST_ASSERT_TRUE(r.tookMs <= QxHunnoxTransport::BUDGET_MS + QX_CTRL_TIMEOUT_MS);
}

void test_budgets_keep_the_poll_step_bound(void) {
    TEST_ASSERT_TRUE(qxStepBoundMs(QxKraulerTransport::BUDGET_MS) <= QX_STEP_BOUND_MAX_MS);
    TEST_ASSERT_TRUE(qxStepBoundMs(QxFabulaTransport::BUDGET_MS) <= QX_STEP_BOUND_MAX_MS);
    TEST_ASSERT_TRUE(qxStepBoundMs(QxHunnoxTransport::BUDGET_MS) <= QX_STEP_BOUND_MAX_MS);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_ascii_reads_the_langid_list_then_the_string);
    RUN_TEST(test_ascii_uses_the_first_langid_of_the_device);
    RUN_TEST(test_ascii_replaces_non_ascii_characters);
    RUN_TEST(test_ascii_truncates_to_buflen_minus_one);
    RUN_TEST(test_ascii_index_zero_is_refused_without_traffic);
    RUN_TEST(test_ascii_malformed_descriptors_are_io_errors);
    RUN_TEST(test_ascii_errors_of_the_primitive_are_returned);
    RUN_TEST(test_ascii_empty_string_is_zero);
    RUN_TEST(test_krauler_indices_and_prefixes);
    RUN_TEST(test_krauler_adds_the_missing_cr);
    RUN_TEST(test_krauler_no_cr_added_without_room);
    RUN_TEST(test_krauler_retries_on_ups_no_ack);
    RUN_TEST(test_krauler_gives_up_after_ten_no_ack);
    RUN_TEST(test_krauler_empty_answer_and_errors_end_at_the_first_attempt);
    RUN_TEST(test_krauler_unknown_command_is_echoed_without_transfers);
    RUN_TEST(test_krauler_retries_stop_at_the_budget);
    RUN_TEST(test_krauler_mute_ups_stays_within_the_budget);
    RUN_TEST(test_fabula_command_indices);
    RUN_TEST(test_fabula_shutdown_encoding);
    RUN_TEST(test_fabula_answers);
    RUN_TEST(test_fabula_unknown_command_is_echoed_without_transfers);
    RUN_TEST(test_fabula_mute_ups_stays_within_the_budget);
    RUN_TEST(test_hunnox_preamble_lock_step_over_five_commands);
    RUN_TEST(test_hunnox_preamble_skips_the_index_being_asked);
    RUN_TEST(test_hunnox_novendor_skips_0x0c_in_the_preamble);
    RUN_TEST(test_hunnox_options_default_reads_0x0c);
    RUN_TEST(test_hunnox_battery_charge_index);
    RUN_TEST(test_hunnox_unknown_command_is_echoed_before_the_preamble);
    RUN_TEST(test_hunnox_shutdown_encoding);
    RUN_TEST(test_hunnox_answer_is_at_most_101_characters_without_added_cr);
    RUN_TEST(test_hunnox_no_ack_zeroes_102_bytes);
    RUN_TEST(test_hunnox_preamble_errors_are_ignored);
    RUN_TEST(test_hunnox_empty_answer_and_errors);
    RUN_TEST(test_hunnox_mute_ups_stays_within_the_budget);
    RUN_TEST(test_budgets_keep_the_poll_step_bound);
    return UNITY_END();
}
