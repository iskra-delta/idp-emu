#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#define CHIPS_IMPL
#include "i8272.h"
#include "s1410.h"
#include "idpartner_sasi.h"
#include "z80dma.h"

namespace {

struct fdc_write_capture {
    bool called = false;
    int drive = -1;
    int c = -1;
    int h = -1;
    int r = -1;
    int n = -1;
    std::array<uint8_t, I8272_SECTOR_SIZE> data{};
    size_t size = 0;
};

struct fdc_read_capture {
    int calls = 0;
    std::array<int, 8> sectors{};
};

struct fdc_format_capture {
    int calls = 0;
    std::array<int, 8> drive{};
    std::array<int, 8> c{};
    std::array<int, 8> h{};
    std::array<int, 8> r{};
    std::array<int, 8> n{};
    bool fill_ok = true;
    uint8_t expected_fill = 0x00;
};

struct hdc_write_capture {
    bool called = false;
    uint32_t lba = 0;
    uint32_t count = 0;
    std::array<uint8_t, 512> data{};
    size_t size = 0;
};

static bool fdc_write_sector_cb(int drive, int c, int h, int r, int n,
                                const uint8_t *buf, void *user)
{
    auto *cap = static_cast<fdc_write_capture *>(user);
    cap->called = true;
    cap->drive = drive;
    cap->c = c;
    cap->h = h;
    cap->r = r;
    cap->n = n;
    cap->size = (n == 0) ? 128u : (size_t)(128u << n);
    if (cap->size > cap->data.size())
        cap->size = cap->data.size();
    std::memcpy(cap->data.data(), buf, cap->size);
    return true;
}

static bool fdc_read_sector_cb(int, int, int, int r, int n, uint8_t *buf, void *user)
{
    auto *cap = static_cast<fdc_read_capture *>(user);
    if (cap->calls < (int)cap->sectors.size())
        cap->sectors[(size_t)cap->calls] = r;
    const size_t size = (n == 0) ? 128u : (size_t)(128u << n);
    for (size_t i = 0; i < size && i < I8272_SECTOR_SIZE; ++i)
        buf[i] = (uint8_t)(r + (int)i);
    ++cap->calls;
    return true;
}

static void s1410_begin_command(s1410_t &hdc)
{
    s1410_write_control(&hdc, 0x01);
    s1410_write_control(&hdc, 0x02);
}

static bool hdc_write_blocks_cb(uint32_t lba, uint32_t count,
                                const uint8_t *src, void *user)
{
    auto *cap = static_cast<hdc_write_capture *>(user);
    cap->called = true;
    cap->lba = lba;
    cap->count = count;
    cap->size = count * 256u;
    if (cap->size > cap->data.size())
        cap->size = cap->data.size();
    std::memcpy(cap->data.data(), src, cap->size);
    return true;
}

static bool fdc_format_sector_cb(int drive, int c, int h, int r, int n,
                                 const uint8_t *buf, void *user)
{
    auto *cap = static_cast<fdc_format_capture *>(user);
    if (cap->calls < (int)cap->drive.size()) {
        cap->drive[(size_t)cap->calls] = drive;
        cap->c[(size_t)cap->calls] = c;
        cap->h[(size_t)cap->calls] = h;
        cap->r[(size_t)cap->calls] = r;
        cap->n[(size_t)cap->calls] = n;
    }
    const size_t size = (n == 0) ? 128u : (size_t)(128u << n);
    for (size_t i = 0; i < size; i++) {
        if (buf[i] != cap->expected_fill) {
            cap->fill_ok = false;
            break;
        }
    }
    cap->calls++;
    return true;
}

static int test_i8272_write_data_command()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    fdc_write_capture cap{};
    i8272_t fdc{};
    i8272_init(&fdc);
    fdc.write_sector = fdc_write_sector_cb;
    fdc.user_data = &cap;
    fdc.drive[0].ready = true;

    const uint8_t cmd[] = {
        I8272_CMD_WRITE_DATA, 0x00, 0x03, 0x01, 0x05, 0x01, 0x05, 0x0A, 0xFF
    };
    for (uint8_t byte : cmd)
        i8272_write_data(&fdc, byte);

    CHECK(fdc.phase == I8272_PHASE_EXECUTE);
    CHECK((fdc.msr & (I8272_MSR_RQM | I8272_MSR_DIO | I8272_MSR_NDM | I8272_MSR_CB)) ==
          (I8272_MSR_RQM | I8272_MSR_CB));
    CHECK(i8272_drq(&fdc));

    for (int i = 0; i < 256; i++) {
        if (i == 255)
            i8272_terminal_count(&fdc);
        i8272_write_data(&fdc, (uint8_t)i);
    }

    CHECK(cap.called);
    CHECK(cap.drive == 0);
    CHECK(cap.c == 0x03);
    CHECK(cap.h == 0x01);
    CHECK(cap.r == 0x05);
    CHECK(cap.n == 0x01);
    CHECK(cap.size == 256u);
    CHECK(cap.data[0] == 0x00);
    CHECK(cap.data[1] == 0x01);
    CHECK(cap.data[255] == 0xFF);
    CHECK(fdc.phase == I8272_PHASE_RESULT);
    CHECK((fdc.st0 & I8272_ST0_IC_MASK) == I8272_ST0_IC_NT);
    CHECK((fdc.st1 & I8272_ST1_EN) != 0u);

    uint8_t result[7]{};
    for (uint8_t &byte : result)
        byte = i8272_read_data(&fdc);

    CHECK(result[0] == 0x00);
    CHECK(result[1] == I8272_ST1_EN);
    CHECK(result[2] == 0x00);
    CHECK(result[3] == 0x03);
    CHECK(result[4] == 0x01);
    CHECK(result[5] == 0x05);
    CHECK(result[6] == 0x01);
    CHECK(fdc.phase == I8272_PHASE_IDLE);

#undef CHECK
    return fails;
}

static int test_i8272_read_data_not_ready()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    fdc_read_capture cap{};
    i8272_t fdc{};
    i8272_init(&fdc);
    fdc.read_sector = fdc_read_sector_cb;
    fdc.user_data = &cap;
    fdc.drive[1].ready = false;

    const uint8_t cmd[] = {
        I8272_CMD_READ_DATA, 0x05, 0x03, 0x01, 0x07, 0x01, 0x07, 0x1B, 0xFF
    };
    for (uint8_t byte : cmd)
        i8272_write_data(&fdc, byte);

    CHECK(cap.calls == 0);
    CHECK(fdc.phase == I8272_PHASE_RESULT);
    CHECK(fdc.data_len == 0u);
    CHECK(fdc.st0 == (I8272_ST0_IC_AT | I8272_ST0_NR | I8272_ST0_HD | I8272_ST0_US0));
    CHECK(fdc.st1 == 0u);
    CHECK(fdc.st2 == 0u);

    uint8_t result[7]{};
    for (uint8_t &byte : result)
        byte = i8272_read_data(&fdc);

    CHECK(result[0] == (I8272_ST0_IC_AT | I8272_ST0_NR | I8272_ST0_HD | I8272_ST0_US0));
    CHECK(result[1] == 0u);
    CHECK(result[2] == 0u);
    CHECK(result[3] == 0x03u);
    CHECK(result[4] == 0x01u);
    CHECK(result[5] == 0x07u);
    CHECK(result[6] == 0x01u);
    CHECK(fdc.phase == I8272_PHASE_IDLE);

#undef CHECK
    return fails;
}

static int test_s1410_write6_command()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    hdc_write_capture cap{};
    s1410_t hdc{};
    s1410_init(&hdc);
    hdc.present = true;
    hdc.write_blocks = hdc_write_blocks_cb;
    hdc.user_data = &cap;

    s1410_begin_command(hdc);
    const uint8_t cmd[] = { 0x0A, 0x00, 0x00, 0x21, 0x01, 0x00 };
    for (uint8_t byte : cmd)
        s1410_write_data(&hdc, byte);

    CHECK(hdc.phase == S1410_PHASE_WRITE_DATA);
    CHECK(hdc.data_len == 256u);
    CHECK(hdc.data_idx == 0u);

    for (int i = 0; i < 256; i++)
        s1410_write_data(&hdc, (uint8_t)(0x80u + (uint8_t)i));

    CHECK(cap.called);
    CHECK(cap.lba == 0x21u);
    CHECK(cap.count == 1u);
    CHECK(cap.size == 256u);
    CHECK(cap.data[0] == 0x80u);
    CHECK(cap.data[1] == 0x81u);
    CHECK(cap.data[255] == 0x7Fu);
    CHECK(hdc.phase == S1410_PHASE_STATUS);
    CHECK(hdc.error == 0x00u);

    CHECK(s1410_read_data(&hdc) == 0x00u);
    CHECK(hdc.phase == S1410_PHASE_MESSAGE);
    CHECK(s1410_read_data(&hdc) == 0x00u);
    CHECK(hdc.phase == S1410_PHASE_IDLE);

#undef CHECK
    return fails;
}

static int test_i8272_format_track_command()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    fdc_format_capture cap{};
    cap.expected_fill = 0xE5;

    i8272_t fdc{};
    i8272_init(&fdc);
    fdc.write_sector = fdc_format_sector_cb;
    fdc.user_data = &cap;
    fdc.drive[0].ready = true;

    const uint8_t cmd[] = {
        I8272_CMD_FORMAT_TRACK, 0x00, 0x01, 0x03, 0x1B, 0xE5
    };
    for (uint8_t byte : cmd)
        i8272_write_data(&fdc, byte);

    CHECK(fdc.phase == I8272_PHASE_EXECUTE);
    CHECK(fdc.data_len == 12u);
    CHECK(fdc.data_idx == 0u);
    CHECK((fdc.msr & (I8272_MSR_RQM | I8272_MSR_DIO | I8272_MSR_NDM | I8272_MSR_CB)) ==
          (I8272_MSR_RQM | I8272_MSR_CB));

    const uint8_t ids[] = {
        0x00, 0x00, 0x01, 0x01,
        0x00, 0x00, 0x02, 0x01,
        0x00, 0x00, 0x03, 0x01,
    };
    for (uint8_t byte : ids)
        i8272_write_data(&fdc, byte);

    CHECK(cap.calls == 3);
    CHECK(cap.fill_ok == true);
    CHECK(cap.drive[0] == 0);
    CHECK(cap.r[0] == 1);
    CHECK(cap.r[1] == 2);
    CHECK(cap.r[2] == 3);
    CHECK(cap.n[2] == 1);
    CHECK(fdc.phase == I8272_PHASE_RESULT);
    CHECK(fdc.st0 == 0x00u);
    CHECK(fdc.st1 == 0x00u);
    CHECK(fdc.st2 == 0x00u);

    uint8_t result[7]{};
    for (uint8_t &byte : result)
        byte = i8272_read_data(&fdc);

    CHECK(result[0] == 0x00u);
    CHECK(result[1] == 0x00u);
    CHECK(result[2] == 0x00u);
    CHECK(result[3] == 0x00u);
    CHECK(result[4] == 0x00u);
    CHECK(result[5] == 0x03u);
    CHECK(result[6] == 0x01u);
    CHECK(fdc.phase == I8272_PHASE_IDLE);

#undef CHECK
    return fails;
}

static int test_sasi_adapter_write_phase_status()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    s1410_t hdc{};
    s1410_init(&hdc);
    hdc.present = true;
    hdc.phase = S1410_PHASE_WRITE_DATA;
    hdc.data_len = 256;
    hdc.data_idx = 0;

    idpartner_sasi_t sasi{};
    idpartner_sasi_init(&sasi, &hdc);
    sasi.session_active = true;
    sasi.data_enable = true;
    sasi.drq_enable = true;
    sasi.sel_latched = false;

    const uint8_t status = idpartner_sasi_status_r(&sasi);
    CHECK((status & 0x80u) != 0u);
    CHECK((status & 0x40u) == 0u);
    CHECK((status & 0x20u) == 0u);
    CHECK((status & 0x10u) == 0u);
    CHECK(idpartner_sasi_drq(&sasi) == true);

#undef CHECK
    return fails;
}

static int test_s1410_initialize_and_completion_phases()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    s1410_t hdc{};
    s1410_init(&hdc);
    s1410_set_present(&hdc, true);
    idpartner_sasi_t sasi{};
    idpartner_sasi_init(&sasi, &hdc);

    idpartner_sasi_ctrl_w(&sasi, 0x01);
    CHECK(idpartner_sasi_status_r(&sasi) == 0x08u);
    idpartner_sasi_ctrl_w(&sasi, 0x02);
    CHECK(idpartner_sasi_status_r(&sasi) == 0x98u);

    const uint8_t command[] = {0x0C, 0, 0, 0, 0, 0};
    for (uint8_t byte : command)
        idpartner_sasi_data_w(&sasi, byte);
    CHECK(hdc.phase == S1410_PHASE_WRITE_DATA);
    CHECK(hdc.data_len == 8u);
    CHECK(idpartner_sasi_status_r(&sasi) == 0x88u);

    const uint8_t characteristics[] = {
        0x01, 0x32, 0x04, 0x00, 0x80, 0x00, 0x40, 0x0B
    };
    for (uint8_t byte : characteristics)
        idpartner_sasi_data_w(&sasi, byte);
    CHECK(hdc.initialized);
    CHECK(hdc.cylinders == 306u);
    CHECK(hdc.heads == 4u);
    CHECK(hdc.reduced_current_cylinder == 128u);
    CHECK(hdc.write_precomp_cylinder == 64u);
    CHECK(hdc.ecc_burst_length == 11u);
    CHECK(hdc.phase == S1410_PHASE_STATUS);
    CHECK(idpartner_sasi_status_r(&sasi) == 0xD8u);

    CHECK(idpartner_sasi_data_r(&sasi) == 0x00u);
    CHECK(hdc.phase == S1410_PHASE_MESSAGE);
    CHECK(idpartner_sasi_status_r(&sasi) == 0xF8u);
    CHECK(idpartner_sasi_data_r(&sasi) == 0x00u);
    CHECK(hdc.phase == S1410_PHASE_IDLE);
    CHECK(!hdc.busy);

#undef CHECK
    return fails;
}

static int test_s1410_invalid_command_and_request_sense()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    s1410_t hdc{};
    s1410_init(&hdc);
    s1410_set_present(&hdc, true);

    s1410_begin_command(hdc);
    const uint8_t invalid[] = {0x12, 0, 0, 0, 0, 0};
    for (uint8_t byte : invalid)
        s1410_write_data(&hdc, byte);
    CHECK(hdc.phase == S1410_PHASE_STATUS);
    CHECK(s1410_read_data(&hdc) == 0x02u);
    CHECK(s1410_read_data(&hdc) == 0x00u);

    s1410_begin_command(hdc);
    const uint8_t sense_command[] = {0x03, 0, 0, 0, 0, 0};
    for (uint8_t byte : sense_command)
        s1410_write_data(&hdc, byte);
    CHECK(hdc.phase == S1410_PHASE_READ_DATA);
    CHECK(hdc.data_len == 4u);
    CHECK(s1410_read_data(&hdc) == 0x20u);
    CHECK(s1410_read_data(&hdc) == 0x00u);
    CHECK(s1410_read_data(&hdc) == 0x00u);
    CHECK(s1410_read_data(&hdc) == 0x00u);
    CHECK(hdc.phase == S1410_PHASE_STATUS);
    CHECK(s1410_read_data(&hdc) == 0x00u);
    CHECK(s1410_read_data(&hdc) == 0x00u);

#undef CHECK
    return fails;
}

static int test_i8272_multisector_terminal_count()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    fdc_read_capture cap{};
    i8272_t fdc{};
    i8272_init(&fdc);
    fdc.read_sector = fdc_read_sector_cb;
    fdc.user_data = &cap;
    fdc.drive[0].ready = true;

    const uint8_t command[] = {
        I8272_CMD_READ_DATA, 0, 0, 0, 1, 1, 3, 0x0A, 0xFF
    };
    for (uint8_t byte : command)
        i8272_write_data(&fdc, byte);
    CHECK(fdc.phase == I8272_PHASE_EXECUTE);
    CHECK(cap.calls == 1);
    CHECK(cap.sectors[0] == 1);

    for (int i = 0; i < 256; ++i)
        (void)i8272_read_data(&fdc);
    CHECK(fdc.phase == I8272_PHASE_EXECUTE);
    CHECK(cap.calls == 2);
    CHECK(cap.sectors[1] == 2);

    for (int i = 0; i < 256; ++i) {
        if (i == 255)
            i8272_terminal_count(&fdc);
        (void)i8272_read_data(&fdc);
    }
    CHECK(fdc.phase == I8272_PHASE_RESULT);
    CHECK((fdc.st0 & I8272_ST0_IC_MASK) == I8272_ST0_IC_NT);
    CHECK(fdc.st1 == 0u);
    CHECK(fdc.result[5] == 2u);

#undef CHECK
    return fails;
}

static int test_i8272_non_dma_and_dtl()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    fdc_read_capture cap{};
    i8272_t fdc{};
    i8272_init(&fdc);
    fdc.read_sector = fdc_read_sector_cb;
    fdc.user_data = &cap;
    fdc.drive[0].ready = true;

    const uint8_t specify[] = {I8272_CMD_SPECIFY, 0xDF, 0x03};
    for (uint8_t byte : specify)
        i8272_write_data(&fdc, byte);
    CHECK(fdc.ndma);

    const uint8_t read[] = {
        I8272_CMD_READ_DATA, 0, 0, 0, 1, 0, 2, 0x1B, 64
    };
    for (uint8_t byte : read)
        i8272_write_data(&fdc, byte);
    CHECK(fdc.data_len == 64u);
    CHECK((fdc.msr & I8272_MSR_NDM) != 0u);
    CHECK(!i8272_drq(&fdc));

#undef CHECK
    return fails;
}

static int test_i8272_drive_status_and_read_id()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    i8272_t fdc{};
    i8272_init(&fdc);
    i8272_set_drive_ready(&fdc, 0, true);
    i8272_set_drive_media(&fdc, 0, true, true, 1);

    i8272_write_data(&fdc, I8272_CMD_SENSE_DRIVE);
    i8272_write_data(&fdc, 0);
    CHECK(i8272_read_data(&fdc) == 0x78u);

    i8272_write_data(&fdc, I8272_CMD_READ_ID);
    i8272_write_data(&fdc, 0);
    uint8_t result[7]{};
    for (uint8_t &byte : result)
        byte = i8272_read_data(&fdc);
    CHECK(result[0] == 0u);
    CHECK(result[3] == 0u);
    CHECK(result[4] == 0u);
    CHECK(result[5] == 1u);
    CHECK(result[6] == 1u);

#undef CHECK
    return fails;
}

static int test_i8272_seek_busy_and_sense_interrupt()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    i8272_t fdc{};
    i8272_init(&fdc);
    const uint8_t seek[] = {I8272_CMD_SEEK, 0x02, 37};
    for (uint8_t byte : seek)
        i8272_write_data(&fdc, byte);
    CHECK((i8272_read_status(&fdc) & I8272_MSR_D2B) != 0u);
    CHECK(!fdc.int_pending);
    for (int i = 0; i < 63; ++i)
        i8272_tick(&fdc);
    CHECK((i8272_read_status(&fdc) & I8272_MSR_D2B) != 0u);
    i8272_tick(&fdc);
    CHECK((i8272_read_status(&fdc) & I8272_MSR_D2B) == 0u);
    CHECK(fdc.int_pending);
    CHECK(fdc.irq_request);

    i8272_write_data(&fdc, I8272_CMD_SENSE_INT);
    CHECK(i8272_read_data(&fdc) == (I8272_ST0_SE | 0x02u));
    CHECK(i8272_read_data(&fdc) == 37u);
    CHECK(!fdc.irq_request);

#undef CHECK
    return fails;
}

static int test_i8272_missing_terminal_count_reaches_eot()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    fdc_write_capture cap{};
    i8272_t fdc{};
    i8272_init(&fdc);
    fdc.write_sector = fdc_write_sector_cb;
    fdc.user_data = &cap;
    fdc.drive[0].ready = true;
    const uint8_t command[] = {
        I8272_CMD_WRITE_DATA, 0, 0, 0, 1, 1, 1, 0x0A, 0xFF
    };
    for (uint8_t byte : command)
        i8272_write_data(&fdc, byte);
    for (int i = 0; i < 256; ++i)
        i8272_write_data(&fdc, (uint8_t)i);
    CHECK(cap.called);
    CHECK(fdc.phase == I8272_PHASE_RESULT);
    CHECK((fdc.st0 & I8272_ST0_IC_MASK) == I8272_ST0_IC_AT);
    CHECK((fdc.st1 & I8272_ST1_EN) != 0u);

#undef CHECK
    return fails;
}

static int test_i8272_read_track_deleted_and_scan_commands()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    fdc_read_capture read_cap{};
    i8272_t fdc{};
    i8272_init(&fdc);
    fdc.read_sector = fdc_read_sector_cb;
    fdc.user_data = &read_cap;
    fdc.drive[0].ready = true;
    const uint8_t read_track[] = {
        I8272_CMD_READ_TRACK, 0, 0, 0, 1, 1, 2, 0x0A, 0xFF
    };
    for (uint8_t byte : read_track)
        i8272_write_data(&fdc, byte);
    for (int i = 0; i < 512; ++i)
        (void)i8272_read_data(&fdc);
    CHECK(read_cap.calls == 2);
    CHECK(fdc.phase == I8272_PHASE_RESULT);
    CHECK((fdc.st0 & I8272_ST0_IC_MASK) == I8272_ST0_IC_NT);
    CHECK(fdc.st1 == 0u);
    for (int i = 0; i < 7; ++i)
        (void)i8272_read_data(&fdc);

    fdc_write_capture write_cap{};
    fdc.write_sector = fdc_write_sector_cb;
    fdc.user_data = &write_cap;
    const uint8_t write_deleted[] = {
        I8272_CMD_WRITE_DELETED, 0, 0, 0, 1, 1, 2, 0x0A, 0xFF
    };
    for (uint8_t byte : write_deleted)
        i8272_write_data(&fdc, byte);
    for (int i = 0; i < 256; ++i) {
        if (i == 255)
            i8272_terminal_count(&fdc);
        i8272_write_data(&fdc, (uint8_t)i);
    }
    CHECK(write_cap.called);
    CHECK(fdc.phase == I8272_PHASE_RESULT);
    for (int i = 0; i < 7; ++i)
        (void)i8272_read_data(&fdc);

    read_cap = {};
    fdc.read_sector = fdc_read_sector_cb;
    fdc.user_data = &read_cap;
    const uint8_t read_deleted[] = {
        I8272_CMD_READ_DELETED, 0, 0, 0, 1, 1, 2, 0x0A, 0xFF
    };
    for (uint8_t byte : read_deleted)
        i8272_write_data(&fdc, byte);
    for (int i = 0; i < 256; ++i) {
        if (i == 255)
            i8272_terminal_count(&fdc);
        (void)i8272_read_data(&fdc);
    }
    CHECK(read_cap.calls == 1);
    CHECK(fdc.phase == I8272_PHASE_RESULT);
    for (int i = 0; i < 7; ++i)
        (void)i8272_read_data(&fdc);

    const uint8_t scan_equal[] = {
        I8272_CMD_SCAN_EQUAL, 0, 0, 0, 1, 1, 1, 0x0A, 1
    };
    for (uint8_t byte : scan_equal)
        i8272_write_data(&fdc, byte);
    for (int i = 0; i < 256; ++i)
        i8272_write_data(&fdc, (uint8_t)(1 + i));
    CHECK(fdc.phase == I8272_PHASE_RESULT);
    CHECK((fdc.st2 & I8272_ST2_SH) != 0u);
    CHECK((fdc.st2 & I8272_ST2_SN) == 0u);

#undef CHECK
    return fails;
}

static int test_partner_dma_compat_direction_trailer()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    const uint8_t prefix[] = {
        0x79, 0x00, 0x51, 0xFF, 0x00, // RAM address 0x5100, 256 bytes
        0x14, 0x28, 0x85, 0xF1, 0x8A, // port A=RAM, port B=FDC data
        0xCF
    };

    z80dma_t dma{};
    z80dma_init(&dma);
    for (uint8_t byte : prefix)
        z80dma_write(&dma, byte);
    z80dma_write(&dma, 0x01); // BIOS read trailer: port B -> port A
    z80dma_write(&dma, 0xCF);
    z80dma_write(&dma, 0x87);
    CHECK(!dma.direction_ab);
    CHECK(dma.port_a.is_memory);
    CHECK(!dma.port_b.is_memory);
    CHECK(dma.port_b.address == 0x00F1u);
    CHECK(dma.port_a.block_length == 256u);
    CHECK(dma.enabled);

    z80dma_reset(&dma);
    for (uint8_t byte : prefix)
        z80dma_write(&dma, byte);
    z80dma_write(&dma, 0x05); // BIOS write trailer: port A -> port B
    z80dma_write(&dma, 0xCF);
    z80dma_write(&dma, 0x87);
    CHECK(dma.direction_ab);
    CHECK(dma.port_a.is_memory);
    CHECK(!dma.port_b.is_memory);
    CHECK(dma.port_b.address == 0x00F1u);
    CHECK(dma.port_a.block_length == 256u);
    CHECK(dma.enabled);

    const uint8_t gdp_hdd_prefix[] = {
        0x79, 0x00, 0xE0, 0xFF, 0x1E, // RAM address 0xE000, 7936 bytes
        0x14, 0x28, 0x95, 0x11, 0x00, 0x8A, // GDP SASI long form
        0xCF
    };
    z80dma_reset(&dma);
    for (uint8_t byte : gdp_hdd_prefix)
        z80dma_write(&dma, byte);
    z80dma_write(&dma, 0x01); // GDP hard-disk read: port B -> port A
    z80dma_write(&dma, 0xCF);
    z80dma_write(&dma, 0x87);
    CHECK(!dma.direction_ab);
    CHECK(dma.port_a.is_memory);
    CHECK(!dma.port_b.is_memory);
    CHECK(dma.port_b.address == 0x0011u);
    CHECK(dma.port_a.address == 0xE000u);
    CHECK(dma.port_a.block_length == 7936u);
    CHECK(dma.enabled);

    const uint8_t crt_prefix[] = {
        0x79, 0x00, 0xE0, 0xFF, 0x00, // RAM address 0xE000, 256 bytes
        0x14, 0x28, 0x95, 0xF1, 0x0C, 0xFF, 0x8A, // CRT ROM long form
        0xCF
    };
    z80dma_reset(&dma);
    for (uint8_t byte : crt_prefix)
        z80dma_write(&dma, byte);
    z80dma_write(&dma, 0x01); // CRT floppy read: port B -> port A
    z80dma_write(&dma, 0xCF);
    z80dma_write(&dma, 0x87);
    CHECK(!dma.direction_ab);
    CHECK(dma.port_a.is_memory);
    CHECK(!dma.port_b.is_memory);
    CHECK(dma.port_b.address == 0x00F1u);
    CHECK(dma.port_a.address == 0xE000u);
    CHECK(dma.port_a.block_length == 256u);
    CHECK(dma.enabled);

#undef CHECK
    return fails;
}

} // namespace

int main()
{
    int fails = 0;
    fails += test_i8272_write_data_command();
    fails += test_i8272_read_data_not_ready();
    fails += test_i8272_format_track_command();
    fails += test_i8272_multisector_terminal_count();
    fails += test_i8272_non_dma_and_dtl();
    fails += test_i8272_drive_status_and_read_id();
    fails += test_i8272_seek_busy_and_sense_interrupt();
    fails += test_i8272_missing_terminal_count_reaches_eot();
    fails += test_i8272_read_track_deleted_and_scan_commands();
    fails += test_s1410_write6_command();
    fails += test_s1410_initialize_and_completion_phases();
    fails += test_s1410_invalid_command_and_request_sense();
    fails += test_sasi_adapter_write_phase_status();
    fails += test_partner_dma_compat_direction_trailer();
    if (fails == 0) {
        std::puts("test_disk_controllers: PASS");
        return 0;
    }
    std::printf("test_disk_controllers: %d failure(s)\n", fails);
    return 1;
}
