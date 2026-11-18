#include <unity.h>
#include <string.h>
#include <string>
#include <vector>
#include "QxUsbCypress.h"
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

// --- langid_fix applied by itself (issue #91) ---

void test_langid_read_asks_buflen_with_the_langid_and_converts(void) {
    FakeQxUsbPrimitives usb(nullptr);
    // 'A', U+0142 (high byte), 'Z'
    usb.script(std::vector<uint8_t>{8, 0x03, 'A', 0, 0x42, 0x01, 'Z', 0});
    char buf[BUF] = {0};
    bool mismatch = true;
    TEST_ASSERT_EQUAL(3, qxGetStringLangid(usb, 0x03, 0x0409, buf, 102, mismatch));
    TEST_ASSERT_FALSE(mismatch);
    TEST_ASSERT_EQUAL_STRING("A?Z", buf);
    TEST_ASSERT_EQUAL(1, (int)usb.stringCalls.size());
    assertCall(usb.stringCalls[0], 0x03, 0x0409, 102);
}

void test_langid_read_size_mismatch_and_errors(void) {
    char buf[BUF];
    bool mismatch = false;
    {
        // bLength 10, 4 bytes received: "size mismatch", buf untouched
        FakeQxUsbPrimitives usb(nullptr);
        usb.script(std::vector<uint8_t>{10, 0x03, 'A', 0});
        memset(buf, 'x', sizeof(buf));
        TEST_ASSERT_EQUAL(0, qxGetStringLangid(usb, 0x03, 0x0409, buf, 102, mismatch));
        TEST_ASSERT_TRUE(mismatch);
        TEST_ASSERT_EQUAL('x', buf[0]);
    }
    {
        // The descriptor type is not checked, as upstream
        FakeQxUsbPrimitives usb(nullptr);
        usb.script(std::vector<uint8_t>{4, 0x02, 'A', 0});
        TEST_ASSERT_EQUAL(1, qxGetStringLangid(usb, 0x03, 0x0409, buf, 102, mismatch));
        TEST_ASSERT_FALSE(mismatch);
    }
    {
        FakeQxUsbPrimitives usb(nullptr);
        usb.scriptError(QX_USB_ERR_PIPE);
        TEST_ASSERT_EQUAL(QX_USB_ERR_PIPE, qxGetStringLangid(usb, 0x03, 0x0409, buf, 102, mismatch));
        TEST_ASSERT_FALSE(mismatch);
    }
    {
        // Only buflen bytes are asked, as upstream: a longer string is a size mismatch
        FakeQxUsbPrimitives usb(nullptr);
        usb.script(FakeQxUsbPrimitives::stringDescriptor("ABCDEF"));
        TEST_ASSERT_EQUAL(0, qxGetStringLangid(usb, 0x03, 0x0409, buf, 4, mismatch));
        TEST_ASSERT_TRUE(mismatch);
        assertCall(usb.stringCalls[0], 0x03, 0x0409, 4);
    }
    {
        // Odd bLength: the last byte is read as a unit with a zero high byte, as upstream (no
        // read past the local buffer)
        FakeQxUsbPrimitives usb(nullptr);
        usb.script(std::vector<uint8_t>{5, 0x03, 'A', 0, 'B'});
        memset(buf, 'x', sizeof(buf));
        TEST_ASSERT_EQUAL(2, qxGetStringLangid(usb, 0x03, 0x0409, buf, 102, mismatch));
        TEST_ASSERT_FALSE(mismatch);
        TEST_ASSERT_EQUAL_STRING("AB", buf);
    }
}

// The VIDA Armour 1200VA case: usb_get_string_simple() refuses the answer (a langid list
// shorter than 4 bytes here), the langid_fix read answers. From then on only that read.
void test_hunnox_switches_to_langid_fix_when_the_simple_read_fails(void) {
    FakeQxUsbPrimitives usb(nullptr);
    QxHunnoxTransport t(usb);
    for (int i = 0; i < 3; i++) usb.script(FakeQxUsbPrimitives::langidDescriptor(0x0409)); // preamble
    usb.script(std::vector<uint8_t>{2, 0x03});                         // string 0 for the simple read
    usb.script(FakeQxUsbPrimitives::stringDescriptor("(246.0 246.0 247.0 007 50.1 27.3 29.0 00001001\r"));
    TEST_ASSERT_EQUAL(0, t.langidFix());
    Run r = send(t, usb, "Q1\r");
    TEST_ASSERT_EQUAL_STRING("(246.0 246.0 247.0 007 50.1 27.3 29.0 00001001\r", r.text.c_str());
    TEST_ASSERT_EQUAL((int)r.text.size(), r.ret);
    TEST_ASSERT_EQUAL(5, (int)usb.stringCalls.size());
    assertCall(usb.stringCalls[3], 0x00, 0x0000, 255);
    assertCall(usb.stringCalls[4], 0x03, 0x0409, 102); // as NUT with langid_fix: buflen 102
    TEST_ASSERT_EQUAL_HEX16(0x0409, t.langidFix());
    // The refused read stays in the diagnostics
    const QxStringFailure& f = t.stringFailure();
    TEST_ASSERT_TRUE(f.present);
    TEST_ASSERT_EQUAL_HEX8(0x03, f.index);
    TEST_ASSERT_EQUAL(QX_USB_ERR_IO, f.result);
    TEST_ASSERT_EQUAL(2, f.langidsLength);
    TEST_ASSERT_EQUAL_HEX8(0x02, f.langids[0]);
    TEST_ASSERT_EQUAL_HEX8(0x03, f.langids[1]);
    TEST_ASSERT_FALSE(f.answerAsked);

    // Next command (preamble step 1: 0x0d), then one read with langid_fix only
    usb.stringCalls.clear();
    usb.script(FakeQxUsbPrimitives::stringDescriptor("x"));
    usb.script(FakeQxUsbPrimitives::stringDescriptor("(246.0\r"));
    r = send(t, usb, "Q1\r");
    TEST_ASSERT_EQUAL_STRING("(246.0\r", r.text.c_str());
    TEST_ASSERT_EQUAL(2, (int)usb.stringCalls.size());
    assertCall(usb.stringCalls[1], 0x03, 0x0409, 102);
}

void test_hunnox_langid_fix_size_mismatch_returns_zero_once_active(void) {
    FakeQxUsbPrimitives usb(nullptr);
    QxHunnoxTransport t(usb);
    for (int i = 0; i < 3; i++) usb.script(FakeQxUsbPrimitives::langidDescriptor(0x0409));
    usb.scriptError(QX_USB_ERR_PIPE);
    usb.script(FakeQxUsbPrimitives::stringDescriptor("(246.0\r"));
    TEST_ASSERT_EQUAL(7, send(t, usb, "Q1\r").ret);
    usb.script(FakeQxUsbPrimitives::stringDescriptor("x"));   // preamble step 1
    usb.script(std::vector<uint8_t>{20, 0x03, '(', 0});       // bLength beyond the bytes
    TEST_ASSERT_EQUAL(0, send(t, usb, "Q1\r").ret);
    TEST_ASSERT_EQUAL_HEX16(0x0409, t.langidFix()); // stays: a corrupt answer, as in NUT
}

void test_hunnox_no_switch_when_the_langid_fix_read_does_not_help(void) {
    {
        // The langid_fix read STALLs too: the error of usb_get_string_simple(), no switch
        FakeQxUsbPrimitives usb(nullptr);
        QxHunnoxTransport t(usb);
        for (int i = 0; i < 3; i++) usb.script(FakeQxUsbPrimitives::langidDescriptor(0x0409));
        usb.script(FakeQxUsbPrimitives::langidDescriptor(0x0407));
        usb.script(std::vector<uint8_t>{10, 0x03, 'A', 0});  // bLength beyond the bytes
        usb.scriptError(QX_USB_ERR_PIPE);
        Run r = send(t, usb, "Q1\r");
        TEST_ASSERT_EQUAL(QX_USB_ERR_IO, r.ret);
        TEST_ASSERT_EQUAL_STRING("", r.text.c_str());
        TEST_ASSERT_EQUAL(0, t.langidFix());
        const QxStringFailure& f = t.stringFailure();
        TEST_ASSERT_TRUE(f.present);
        TEST_ASSERT_EQUAL(4, f.langidsLength);
        TEST_ASSERT_EQUAL_HEX8(0x07, f.langids[2]);
        TEST_ASSERT_TRUE(f.answerAsked);
        TEST_ASSERT_EQUAL(4, f.answerLength);
        TEST_ASSERT_EQUAL_HEX8(10, f.answer[0]);
        assertCall(usb.stringCalls[4], 0x03, 0x0407, 255);
        assertCall(usb.stringCalls[5], 0x03, 0x0409, 102);
    }
    {
        // An empty answer or a size mismatch with langid_fix: no switch either
        FakeQxUsbPrimitives usb(nullptr);
        QxHunnoxTransport t(usb);
        for (int i = 0; i < 3; i++) usb.script(FakeQxUsbPrimitives::langidDescriptor(0x0409));
        usb.scriptError(QX_USB_ERR_PIPE);
        usb.script(FakeQxUsbPrimitives::stringDescriptor(""));
        TEST_ASSERT_EQUAL(QX_USB_ERR_PIPE, send(t, usb, "Q1\r").ret);
        TEST_ASSERT_EQUAL(0, t.langidFix());
        usb.script(FakeQxUsbPrimitives::stringDescriptor("x"));
        usb.scriptError(QX_USB_ERR_PIPE);
        usb.script(std::vector<uint8_t>{20, 0x03, '(', 0});
        TEST_ASSERT_EQUAL(QX_USB_ERR_PIPE, send(t, usb, "Q1\r").ret);
        TEST_ASSERT_EQUAL(0, t.langidFix());
    }
}

void test_simple_read_that_works_never_tries_langid_fix(void) {
    FakeQxUsbPrimitives usb(MEGATEC_TESTING);
    usb.stringDevice = true;
    usb.langidList = 0x0407;
    QxHunnoxTransport t(usb);
    for (int i = 0; i < 4; i++) TEST_ASSERT_TRUE(send(t, usb, "Q1\r").ret > 0);
    for (const Call& c : usb.stringCalls) {
        // Only the simple reads (255 bytes) and the preamble (1026 / 102 on other indices)
        if (c.index == 0x03) TEST_ASSERT_EQUAL_UINT32(255, (uint32_t)c.buflen);
    }
    TEST_ASSERT_EQUAL(0, t.langidFix());
    TEST_ASSERT_FALSE(t.stringFailure().present);
}

void test_no_langid_fix_read_when_the_device_is_gone(void) {
    FakeQxUsbPrimitives usb(nullptr);
    QxKraulerTransport t(usb);
    usb.scriptError(QX_USB_ERR_NO_DEVICE);
    TEST_ASSERT_EQUAL(QX_USB_ERR_NO_DEVICE, send(t, usb, "Q1\r").ret);
    TEST_ASSERT_EQUAL(1, (int)usb.stringCalls.size());
    TEST_ASSERT_EQUAL(0, t.langidFix());
}

void test_krauler_switches_to_langid_fix_and_retries_a_size_mismatch(void) {
    FakeQxUsbPrimitives usb(nullptr);
    QxKraulerTransport t(usb);
    usb.script(std::vector<uint8_t>{2, 0x03});                    // langid list refused
    usb.script(FakeQxUsbPrimitives::stringDescriptor("x230.0"));  // langid_fix answers
    Run r = send(t, usb, "F\r");
    TEST_ASSERT_EQUAL_STRING("#230.0\r", r.text.c_str()); // prefix and missing CR as before
    TEST_ASSERT_EQUAL_HEX16(0x0409, t.langidFix());
    assertCall(usb.stringCalls[1], 0x0d, 0x0409, BUF < 255 ? BUF : 255);
    // Active: a size mismatch is retried (continue), as krauler_command() with langid_fix
    usb.stringCalls.clear();
    usb.script(std::vector<uint8_t>{20, 0x03, 'x', 0});
    usb.script(FakeQxUsbPrimitives::stringDescriptor("x231.0\r"));
    r = send(t, usb, "F\r");
    TEST_ASSERT_EQUAL_STRING("#231.0\r", r.text.c_str());
    TEST_ASSERT_EQUAL(2, (int)usb.stringCalls.size());
}

void test_fabula_has_no_langid_fix(void) {
    FakeQxUsbPrimitives usb(nullptr);
    QxFabulaTransport t(usb);
    usb.script(std::vector<uint8_t>{2, 0x03});
    usb.script(FakeQxUsbPrimitives::stringDescriptor("(230.0\r"));
    TEST_ASSERT_EQUAL(QX_USB_ERR_IO, send(t, usb, "Q1\r").ret);
    TEST_ASSERT_EQUAL(1, (int)usb.stringCalls.size());
    TEST_ASSERT_EQUAL(0, t.langidFix());
}

// --- cypress (US-141): cypress_command(), nutdrv_qx.c:734-812 ---

namespace {

const char Q1_ANSWER[] = "(215.0 195.0 230.0 014 49.0 22.7 30.0 00000000\r"; // 47 bytes

void assertBlock(const FakeQxUsbPrimitives::ReportCall& c, const char* bytes, size_t n) {
    TEST_ASSERT_EQUAL_HEX8(0x02, c.type); // Output: wValue 0x200 with report ID 0
    TEST_ASSERT_EQUAL_HEX8(0x00, c.id);
    TEST_ASSERT_EQUAL(8, (int)c.bytes.size());
    for (size_t i = 0; i < 8; i++) {
        TEST_ASSERT_EQUAL_HEX8(i < n ? (uint8_t)bytes[i] : 0, c.bytes[i]);
    }
}

} // namespace

void test_cypress_one_block_command_and_multi_chunk_answer(void) {
    FakeQxUsbPrimitives usb(MEGATEC_TESTING);
    QxCypressTransport t(usb);
    Run r = send(t, usb, "Q1\r");
    // One 8-byte block, padded with zeros
    TEST_ASSERT_EQUAL(1, (int)usb.reportCalls.size());
    assertBlock(usb.reportCalls[0], "Q1\r", 3);
    // 47 bytes in 8-byte chunks: 6 reads of 1000 ms each, the last one carries the CR
    TEST_ASSERT_EQUAL(47, r.ret);
    TEST_ASSERT_EQUAL_STRING(Q1_ANSWER, r.text.c_str());
    TEST_ASSERT_EQUAL(6, (int)usb.readTimeouts.size());
    for (uint32_t ms : usb.readTimeouts) TEST_ASSERT_EQUAL_UINT32(1000, ms);
    TEST_ASSERT_EQUAL(1, (int)usb.received().size());
    TEST_ASSERT_EQUAL_STRING("Q1\r", usb.received()[0].c_str());
}

void test_cypress_long_command_goes_out_in_two_blocks(void) {
    FakeQxUsbPrimitives usb(MEGATEC_TESTING); // no rejected answer: the fake echoes
    QxCypressTransport t(usb);
    Run r = send(t, usb, "S.5R0001\r");
    TEST_ASSERT_EQUAL(2, (int)usb.reportCalls.size());
    assertBlock(usb.reportCalls[0], "S.5R0001", 8);
    assertBlock(usb.reportCalls[1], "\r", 1);
    TEST_ASSERT_EQUAL_STRING("S.5R0001\r", usb.received()[0].c_str());
    TEST_ASSERT_EQUAL(9, r.ret);
    TEST_ASSERT_EQUAL_STRING("S.5R0001\r", r.text.c_str());
}

void test_cypress_cr_in_the_first_chunk_ends_the_read(void) {
    FakeQxUsbPrimitives usb(MEGATEC_TESTING);
    usb.device.override("Q\r", "#OK\r");
    QxCypressTransport t(usb);
    Run r = send(t, usb, "Q\r");
    TEST_ASSERT_EQUAL(4, r.ret);
    TEST_ASSERT_EQUAL_STRING("#OK\r", r.text.c_str());
    TEST_ASSERT_EQUAL(1, usb.reads);
}

void test_cypress_full_buffer_without_cr_stops_reading(void) {
    FakeQxUsbPrimitives usb(MEGATEC_TESTING);
    usb.device.override("Q1\r", "AAAAAAAABBBBBBBBCCCCCCCCDDDDDDDD"); // 4 chunks, no CR
    QxCypressTransport t(usb);
    char buf[32] = {0}; // buflen 24: reads at 0, 8 and 16, then i = 24 > buflen - 8; buf[24] ends the text
    Run r = send(t, usb, "Q1\r", buf, 24);
    TEST_ASSERT_EQUAL(24, r.ret);
    TEST_ASSERT_EQUAL_STRING("AAAAAAAABBBBBBBBCCCCCCCC", r.text.c_str());
    TEST_ASSERT_EQUAL(3, usb.reads);
}

void test_cypress_set_report_error_is_returned_without_reads(void) {
    FakeQxUsbPrimitives usb(MEGATEC_TESTING);
    usb.failNextSetReport = QX_USB_ERR_PIPE;
    QxCypressTransport t(usb);
    Run r = send(t, usb, "Q1\r");
    TEST_ASSERT_EQUAL(QX_USB_ERR_PIPE, r.ret);
    TEST_ASSERT_EQUAL(0, usb.reads);
    TEST_ASSERT_TRUE(usb.received().empty());
}

void test_cypress_read_timeout_is_returned(void) {
    FakeQxUsbPrimitives usb(MEGATEC_TESTING);
    usb.device.override("Q1\r", ""); // the UPS takes the command and says nothing
    QxCypressTransport t(usb);
    Run r = send(t, usb, "Q1\r");
    TEST_ASSERT_EQUAL(QX_USB_ERR_TIMEOUT, r.ret);
    TEST_ASSERT_EQUAL(1, usb.reads);
    TEST_ASSERT_TRUE(r.tookMs >= 1000);
    TEST_ASSERT_TRUE(r.tookMs <= 1000 + 10);
}

void test_cypress_mute_ups_stays_within_the_budget(void) {
    FakeQxUsbPrimitives usb(MEGATEC_TESTING);
    usb.muted = true;
    QxCypressTransport t(usb);
    Run r = send(t, usb, "S.5R0001\r");
    TEST_ASSERT_TRUE(r.ret < 0);
    TEST_ASSERT_TRUE(r.tookMs <= QxCypressTransport::BUDGET_MS + QX_CTRL_TIMEOUT_MS);
    TEST_ASSERT_EQUAL(0, usb.reads);
}

void test_cypress_chunks_received_before_the_command_are_dropped(void) {
    FakeQxUsbPrimitives usb(MEGATEC_TESTING);
    usb.queueChunk("(999.9 \r"); // tail of a late answer, or an unsolicited chunk
    QxCypressTransport t(usb);
    Run r = send(t, usb, "Q1\r");
    TEST_ASSERT_EQUAL(1, usb.discards);
    TEST_ASSERT_EQUAL(47, r.ret);
    TEST_ASSERT_EQUAL_STRING(Q1_ANSWER, r.text.c_str());
}

void test_cypress_buffer_shorter_than_a_chunk_fails_without_traffic(void) {
    FakeQxUsbPrimitives usb(MEGATEC_TESTING);
    QxCypressTransport t(usb);
    char buf[8];
    Run r = send(t, usb, "Q1\r", buf, 7);
    TEST_ASSERT_EQUAL(QX_USB_ERR_IO, r.ret);
    TEST_ASSERT_EQUAL(0, usb.setReports);
    TEST_ASSERT_EQUAL(0, usb.reads);
    TEST_ASSERT_EQUAL(0, usb.discards);
}

void test_cypress_identity(void) {
    FakeQxUsbPrimitives usb(nullptr);
    QxCypressTransport t(usb);
    TEST_ASSERT_EQUAL_STRING("cypress", t.name());
    TEST_ASSERT_EQUAL_UINT32(5250, t.commandBudgetMs());
    TEST_ASSERT_TRUE(t.needsInterruptIn());
}

// --- cypress, 0665:5161 quirk (US-141): NUT PR #3563 and #3720, nutdrv_qx.c:782-856 ---

namespace {

const char F_ANSWER[] = "#230.0 000 024.0 50.0\r";

// A cypress transport on 0665:5161, the quirk on as the default of upsdrv_initups()
struct QuirkRig {
    FakeQxUsbPrimitives usb{MEGATEC_TESTING};
    QxCypressTransport t{usb};
    QuirkRig() { t.setDevice(0x0665, 0x5161); }
};

} // namespace

void test_cypress_quirk_follows_the_usb_id(void) {
    FakeQxUsbPrimitives usb(nullptr);
    QxCypressTransport t(usb);
    TEST_ASSERT_FALSE(t.quirk()); // no device yet
    t.setDevice(0x0665, 0x5161);
    TEST_ASSERT_TRUE(t.quirk());
    // The other cypress rows of qx_usb_id[] (Agiler 05b8:0000, Phoenixtec 06da:0002...)
    t.setDevice(0x06da, 0x0002);
    TEST_ASSERT_FALSE(t.quirk());
    t.setDevice(0x0665, 0x5162);
    TEST_ASSERT_FALSE(t.quirk());
}

// An idle endpoint: one 25 ms read confirms it, then the reply chunks wait up to 3000 ms each
void test_cypress_quirk_idle_endpoint_costs_one_short_read(void) {
    QuirkRig r;
    Run x = send(r.t, r.usb, "Q1\r");
    TEST_ASSERT_EQUAL(47, x.ret);
    TEST_ASSERT_EQUAL_STRING(Q1_ANSWER, x.text.c_str());
    TEST_ASSERT_EQUAL(7, (int)r.usb.readTimeouts.size());
    TEST_ASSERT_EQUAL_UINT32(25, r.usb.readTimeouts[0]);
    for (size_t i = 1; i < r.usb.readTimeouts.size(); i++) TEST_ASSERT_EQUAL_UINT32(3000, r.usb.readTimeouts[i]);
    TEST_ASSERT_NULL(r.t.takeNotice());
}

// Without the quirk the reply chunks keep the 1000 ms of CYPRESS_REPLY_TIMEOUT and no drain read
void test_cypress_without_quirk_has_no_drain_read(void) {
    FakeQxUsbPrimitives usb(MEGATEC_TESTING);
    QxCypressTransport t(usb);
    t.setDevice(0x06da, 0x0002);
    usb.device.override("Q1\r", "");
    Run x = send(t, usb, "Q1\r");
    TEST_ASSERT_EQUAL(QX_USB_ERR_TIMEOUT, x.ret);
    TEST_ASSERT_EQUAL(1, (int)usb.readTimeouts.size());
    TEST_ASSERT_EQUAL_UINT32(1000, usb.readTimeouts[0]);
}

void test_cypress_quirk_reply_timeout_is_3000_ms(void) {
    QuirkRig r;
    r.usb.device.override("Q1\r", "");
    Run x = send(r.t, r.usb, "Q1\r");
    TEST_ASSERT_EQUAL(QX_USB_ERR_TIMEOUT, x.ret);
    TEST_ASSERT_EQUAL(2, (int)r.usb.readTimeouts.size());
    TEST_ASSERT_EQUAL_UINT32(3000, r.usb.readTimeouts[1]);
    TEST_ASSERT_TRUE(x.tookMs >= 25 + 3000);
    TEST_ASSERT_TRUE(x.tookMs <= 25 + 3000 + 10);
}

// The field failure of NUT PR #3720: a reply that came after its command timed out must not be
// taken for the reply of the next command. The drain waits for the rest of it (500 ms quiet)
void test_cypress_quirk_late_reply_is_drained_not_taken_for_the_next(void) {
    QuirkRig r;
    r.usb.answerDelayMs = 3500; // later than the 3000 ms reply timeout
    Run first = send(r.t, r.usb, "Q1\r");
    TEST_ASSERT_EQUAL(QX_USB_ERR_TIMEOUT, first.ret);
    TEST_ASSERT_EQUAL(6, (int)r.usb.queuedChunks()); // the Q1 reply, still on its way
    r.usb.answerDelayMs = 300; // a UPS takes 80-408 ms to start answering (#3720)
    r.usb.readTimeouts.clear();
    Run second = send(r.t, r.usb, "F\r");
    TEST_ASSERT_EQUAL_STRING(F_ANSWER, second.text.c_str());
    TEST_ASSERT_EQUAL((int)strlen(F_ANSWER), second.ret);
    // A reply was outstanding: the first drain read already waits 500 ms
    TEST_ASSERT_EQUAL_UINT32(500, r.usb.readTimeouts[0]);
    // The rest of an unread reply is expected: no notice
    TEST_ASSERT_NULL(r.t.takeNotice());
    // Without the quirk, a late reply arriving after the next command leads its answer (what
    // #3720 saw in the field): the Q1 reply comes 100 ms after F was sent, F's 200 ms later
    FakeQxUsbPrimitives usb(MEGATEC_TESTING);
    QxCypressTransport plain(usb);
    usb.answerDelayMs = 1100; // later than the 1000 ms reply timeout
    TEST_ASSERT_EQUAL(QX_USB_ERR_TIMEOUT, send(plain, usb, "Q1\r").ret);
    usb.answerDelayMs = 300;
    TEST_ASSERT_EQUAL('(', send(plain, usb, "F\r").text[0]);
}

// Data the UPS sent unasked: dropped, logged once, then again only after an idle endpoint
void test_cypress_quirk_unsolicited_data_is_logged_once(void) {
    QuirkRig r;
    r.usb.queueChunk("(999.9 \r");
    Run x = send(r.t, r.usb, "Q1\r");
    TEST_ASSERT_EQUAL_STRING(Q1_ANSWER, x.text.c_str());
    TEST_ASSERT_EQUAL(1, (int)r.usb.discarded);
    // After stale data the drain waits 500 ms of quiet, as upstream after its first report
    TEST_ASSERT_EQUAL_UINT32(500, r.usb.readTimeouts[0]);
    const char* n = r.t.takeNotice();
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_EQUAL_STRING(
        "[Q*] cypress: discarded 1 stale input report(s) before Q1: replies were out of step with commands", n);
    TEST_ASSERT_NULL(r.t.takeNotice()); // taken once
    // Again: already reported
    r.usb.queueChunk("(999.9 \r");
    send(r.t, r.usb, "Q1\r");
    TEST_ASSERT_NULL(r.t.takeNotice());
    // An idle endpoint before a command ends the report
    send(r.t, r.usb, "Q1\r");
    TEST_ASSERT_NULL(r.t.takeNotice());
    r.usb.queueChunk("(999.9 \r");
    r.usb.queueChunk("(999.9 \r");
    send(r.t, r.usb, "Q1\r");
    n = r.t.takeNotice();
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_TRUE(strstr(n, "discarded 2 stale input report(s) before Q1") != nullptr);
}

// Stale data arriving during the drain counts too, read by read
void test_cypress_quirk_stale_data_arriving_during_the_drain(void) {
    QuirkRig r;
    r.usb.queueChunkAt(r.usb.clock + 10, "(999.9 \r");
    r.usb.queueChunkAt(r.usb.clock + 200, "(999.9 \r");
    Run x = send(r.t, r.usb, "Q1\r");
    TEST_ASSERT_EQUAL_STRING(Q1_ANSWER, x.text.c_str());
    // 25 ms (finds the first), 500 ms (the second), 500 ms quiet, then the reply
    TEST_ASSERT_EQUAL_UINT32(25, r.usb.readTimeouts[0]);
    TEST_ASSERT_EQUAL_UINT32(500, r.usb.readTimeouts[1]);
    TEST_ASSERT_EQUAL_UINT32(500, r.usb.readTimeouts[2]);
    TEST_ASSERT_EQUAL_UINT32(3000, r.usb.readTimeouts[3]);
    const char* n = r.t.takeNotice();
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_TRUE(strstr(n, "discarded 2 stale") != nullptr);
}

// An empty report is no reply either: discarded like any other
void test_cypress_quirk_empty_report_is_discarded(void) {
    QuirkRig r;
    r.usb.queueChunkAt(r.usb.clock + 5, "");
    Run x = send(r.t, r.usb, "Q1\r");
    TEST_ASSERT_EQUAL(47, x.ret);
    TEST_ASSERT_EQUAL_STRING(Q1_ANSWER, x.text.c_str());
}

// An endpoint that keeps sending: QX_USB_ERR_DESYNC after 32 reports, no command sent
void test_cypress_quirk_busy_endpoint_gives_desync_after_32_reports(void) {
    QuirkRig r;
    for (uint32_t k = 1; k <= 40; k++) r.usb.queueChunkAt(r.usb.clock + 10 * k, "(999.9 \r");
    Run x = send(r.t, r.usb, "Q1\r");
    TEST_ASSERT_EQUAL(QX_USB_ERR_DESYNC, x.ret);
    TEST_ASSERT_EQUAL(0, r.usb.setReports);
    TEST_ASSERT_TRUE(r.usb.received().empty());
    TEST_ASSERT_EQUAL(33, r.usb.reads); // once more than FLUSH_REPORTS
}

// ... or after 3 s of reports, whichever comes first
void test_cypress_quirk_busy_endpoint_gives_desync_after_3_seconds(void) {
    QuirkRig r;
    for (uint32_t k = 1; k <= 30; k++) r.usb.queueChunkAt(r.usb.clock + 10 + 200 * k, "(999.9 \r");
    r.usb.queueChunk("(999.9 \r"); // queued already: the drain starts with 500 ms reads
    Run x = send(r.t, r.usb, "Q1\r");
    TEST_ASSERT_EQUAL(QX_USB_ERR_DESYNC, x.ret);
    TEST_ASSERT_EQUAL(0, r.usb.setReports);
    TEST_ASSERT_TRUE(r.usb.reads < 32);
    TEST_ASSERT_TRUE(x.tookMs > QxCypressTransport::FLUSH_BUDGET_MS);
    TEST_ASSERT_TRUE(x.tookMs <= QxCypressTransport::FLUSH_BUDGET_MS + 250);
}

// The chunks queued before the drain do not count towards the 32 reports (see the class)
void test_cypress_quirk_many_queued_chunks_are_no_desync(void) {
    QuirkRig r;
    for (int k = 0; k < 40; k++) r.usb.queueChunk("(999.9 \r");
    Run x = send(r.t, r.usb, "Q1\r");
    TEST_ASSERT_EQUAL(47, x.ret);
    TEST_ASSERT_EQUAL(40, (int)r.usb.discarded);
    const char* n = r.t.takeNotice();
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_TRUE(strstr(n, "discarded 40 stale") != nullptr);
}

// The readable start of a command names it in the notice (CRC bytes of some protocols)
void test_cypress_quirk_notice_names_only_the_readable_start(void) {
    QuirkRig r;
    r.usb.queueChunk("(999.9 \r");
    send(r.t, r.usb, "QPI\x01\x02\r");
    const char* n = r.t.takeNotice();
    TEST_ASSERT_NOT_NULL(n);
    TEST_ASSERT_TRUE(strstr(n, "before QPI: replies") != nullptr);
}

void test_cypress_quirk_mute_ups_stays_within_the_budget(void) {
    QuirkRig r;
    r.usb.muted = true;
    Run x = send(r.t, r.usb, "S.5R0001\r");
    TEST_ASSERT_TRUE(x.ret < 0);
    TEST_ASSERT_TRUE(x.tookMs <= QxCypressTransport::BUDGET_MS + QX_CTRL_TIMEOUT_MS);
}

void test_budgets_keep_the_poll_step_bound(void) {
    TEST_ASSERT_TRUE(qxStepBoundMs(QxKraulerTransport::BUDGET_MS) <= QX_STEP_BOUND_MAX_MS);
    TEST_ASSERT_TRUE(qxStepBoundMs(QxFabulaTransport::BUDGET_MS) <= QX_STEP_BOUND_MAX_MS);
    TEST_ASSERT_TRUE(qxStepBoundMs(QxHunnoxTransport::BUDGET_MS) <= QX_STEP_BOUND_MAX_MS);
    TEST_ASSERT_TRUE(qxStepBoundMs(QxCypressTransport::BUDGET_MS) <= QX_STEP_BOUND_MAX_MS);
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
    RUN_TEST(test_langid_read_asks_buflen_with_the_langid_and_converts);
    RUN_TEST(test_langid_read_size_mismatch_and_errors);
    RUN_TEST(test_hunnox_switches_to_langid_fix_when_the_simple_read_fails);
    RUN_TEST(test_hunnox_langid_fix_size_mismatch_returns_zero_once_active);
    RUN_TEST(test_hunnox_no_switch_when_the_langid_fix_read_does_not_help);
    RUN_TEST(test_simple_read_that_works_never_tries_langid_fix);
    RUN_TEST(test_no_langid_fix_read_when_the_device_is_gone);
    RUN_TEST(test_krauler_switches_to_langid_fix_and_retries_a_size_mismatch);
    RUN_TEST(test_fabula_has_no_langid_fix);
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
    RUN_TEST(test_cypress_one_block_command_and_multi_chunk_answer);
    RUN_TEST(test_cypress_long_command_goes_out_in_two_blocks);
    RUN_TEST(test_cypress_cr_in_the_first_chunk_ends_the_read);
    RUN_TEST(test_cypress_full_buffer_without_cr_stops_reading);
    RUN_TEST(test_cypress_quirk_follows_the_usb_id);
    RUN_TEST(test_cypress_quirk_idle_endpoint_costs_one_short_read);
    RUN_TEST(test_cypress_without_quirk_has_no_drain_read);
    RUN_TEST(test_cypress_quirk_reply_timeout_is_3000_ms);
    RUN_TEST(test_cypress_quirk_late_reply_is_drained_not_taken_for_the_next);
    RUN_TEST(test_cypress_quirk_unsolicited_data_is_logged_once);
    RUN_TEST(test_cypress_quirk_stale_data_arriving_during_the_drain);
    RUN_TEST(test_cypress_quirk_empty_report_is_discarded);
    RUN_TEST(test_cypress_quirk_busy_endpoint_gives_desync_after_32_reports);
    RUN_TEST(test_cypress_quirk_busy_endpoint_gives_desync_after_3_seconds);
    RUN_TEST(test_cypress_quirk_many_queued_chunks_are_no_desync);
    RUN_TEST(test_cypress_quirk_notice_names_only_the_readable_start);
    RUN_TEST(test_cypress_quirk_mute_ups_stays_within_the_budget);
    RUN_TEST(test_cypress_set_report_error_is_returned_without_reads);
    RUN_TEST(test_cypress_read_timeout_is_returned);
    RUN_TEST(test_cypress_mute_ups_stays_within_the_budget);
    RUN_TEST(test_cypress_chunks_received_before_the_command_are_dropped);
    RUN_TEST(test_cypress_buffer_shorter_than_a_chunk_fails_without_traffic);
    RUN_TEST(test_cypress_identity);
    RUN_TEST(test_budgets_keep_the_poll_step_bound);
    return UNITY_END();
}
