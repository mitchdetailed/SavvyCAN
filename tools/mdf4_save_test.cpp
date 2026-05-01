// Standalone MDF4 save+read round-trip test — mimics saveMDF4File logic exactly.
// Build: g++ -std=c++17 -I../third_party/mdflib/include mdf4_save_test.cpp
//        -L../third_party/mdflib/build/mdflib -lmdf
//        -L"C:/Program Files/mingw64/x86_64-w64-mingw32/lib" -lz
//        -L"C:/Program Files/mingw64/opt/lib" -lexpat
//        -o mdf4_save_test.exe
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <vector>
#include <mdf/mdffactory.h>
#include <mdf/mdfwriter.h>
#include <mdf/mdfreader.h>
#include <mdf/mdffile.h>
#include <mdf/ifilehistory.h>
#include <mdf/iheader.h>
#include <mdf/idatagroup.h>
#include <mdf/ichannelgroup.h>
#include <mdf/ichannelobserver.h>
#include <mdf/canmessage.h>

using namespace mdf;

struct TestFrame {
    uint64_t time_ns;   // absolute ns since epoch (first frame = base)
    uint32_t id;
    bool     extended;
    bool     remote;
    bool     rx;
    int      bus;
    std::vector<uint8_t> data;
};

static void saveTestFile(const std::string& path,
                         uint64_t base_ns,
                         const std::vector<TestFrame>& frames)
{
    auto writer = MdfFactory::CreateMdfWriter(MdfWriterType::MdfConverter);
    writer->Init(path);

    auto* header  = writer->Header();
    auto* history = header->CreateFileHistory();
    history->Time(base_ns);
    history->Description("SavvyCAN round-trip test");
    history->ToolName("SavvyCAN");
    history->ToolVersion("2.0");
    history->UserName("");

    const bool has_remote = std::any_of(frames.begin(), frames.end(),
        [](const TestFrame& f){ return f.remote; });

    writer->BusType(MdfBusType::CAN);
    writer->StorageType(MdfStorageType::MlsdStorage);
    writer->MaxLength(8);
    writer->MandatoryMembersOnly(!has_remote);
    writer->CreateBusLogConfiguration();
    writer->PreTrigTime(0.0);
    writer->CompressData(false);

    auto* last_dg = header->LastDataGroup();
    auto* cg_data   = last_dg->GetChannelGroup("CAN_DataFrame");
    auto* cg_remote = has_remote ? last_dg->GetChannelGroup("CAN_RemoteFrame") : nullptr;

    writer->InitMeasurement();
    writer->StartMeasurement(base_ns);

    for (const auto& f : frames) {
        CanMessage msg;
        msg.BusChannel(static_cast<uint8_t>(f.bus + 1));
        msg.MessageId(f.id);
        msg.ExtendedId(f.extended);
        msg.Dir(!f.rx);

        if (f.remote) {
            msg.Rtr(true);
            msg.DataBytes(std::vector<uint8_t>(f.data.size(), 0x00));
            writer->SaveCanMessage(*cg_remote, f.time_ns, msg);
        } else {
            msg.DataBytes(f.data);
            writer->SaveCanMessage(*cg_data, f.time_ns, msg);
        }
    }

    uint64_t stop_ns = frames.empty() ? base_ns : frames.back().time_ns;
    writer->StopMeasurement(stop_ns);
    writer->FinalizeMeasurement();
}

static void readAndPrint(const std::string& path, double start_s)
{
    MdfReader reader(path);
    reader.ReadEverythingButData();

    const auto* mdf_file = reader.GetFile();
    if (!mdf_file) { std::cerr << "Failed to open " << path << "\n"; return; }

    DataGroupList dg_list;
    mdf_file->DataGroups(dg_list);

    for (auto* dg : dg_list) {
        for (auto* cg : dg->ChannelGroups()) {
            auto* cn_id = cg->GetChannel("CAN_DataFrame.ID");
            bool is_remote = false;
            if (!cn_id) { cn_id = cg->GetChannel("CAN_RemoteFrame.ID"); is_remote = true; }
            if (!cn_id) continue;

            auto* cn_time    = cg->GetMasterChannel();
            auto* cn_ide     = cg->GetChannel(".IDE");
            auto* cn_bus     = cg->GetChannel(".BusChannel");
            auto* cn_dir     = cg->GetChannel(".Dir");
            auto* cn_dlc     = cg->GetChannel(".DLC");
            auto* cn_datalen = cg->GetChannel(".DataLength");
            auto* cn_data    = is_remote ? nullptr : cg->GetChannel(".DataBytes");
            auto* cn_srr     = cg->GetChannel(".SRR");

            auto obs_time    = cn_time    ? CreateChannelObserver(*dg, *cg, *cn_time)    : nullptr;
            auto obs_id      =              CreateChannelObserver(*dg, *cg, *cn_id);
            auto obs_ide     = cn_ide     ? CreateChannelObserver(*dg, *cg, *cn_ide)     : nullptr;
            auto obs_bus     = cn_bus     ? CreateChannelObserver(*dg, *cg, *cn_bus)     : nullptr;
            auto obs_dir     = cn_dir     ? CreateChannelObserver(*dg, *cg, *cn_dir)     : nullptr;
            auto obs_dlc     = cn_dlc     ? CreateChannelObserver(*dg, *cg, *cn_dlc)     : nullptr;
            auto obs_datalen = cn_datalen ? CreateChannelObserver(*dg, *cg, *cn_datalen) : nullptr;
            auto obs_data    = cn_data    ? CreateChannelObserver(*dg, *cg, *cn_data)    : nullptr;
            auto obs_srr     = cn_srr     ? CreateChannelObserver(*dg, *cg, *cn_srr)     : nullptr;

            reader.ReadData(*dg);

            uint64_t n = obs_id->NofSamples();
            std::cout << "\nCG '" << cg->Name() << "' -> " << n << " samples\n";

            for (uint64_t s = 0; s < n; ++s) {
                uint32_t raw_id = 0; obs_id->GetChannelValue(s, raw_id);
                uint8_t ide = 0;     if (obs_ide) obs_ide->GetChannelValue(s, ide);
                uint8_t bus = 0;     if (obs_bus) obs_bus->GetChannelValue(s, bus);
                uint8_t dir = 0;     if (obs_dir) obs_dir->GetChannelValue(s, dir);
                uint8_t dlc = 0;     if (obs_dlc) obs_dlc->GetChannelValue(s, dlc);
                uint8_t dlen = 0;    if (obs_datalen) obs_datalen->GetChannelValue(s, dlen);
                uint8_t srr = 0;     if (obs_srr) obs_srr->GetChannelValue(s, srr);
                std::vector<uint8_t> data;
                if (obs_data) obs_data->GetChannelValue(s, data);

                double rel_s = 0.0;
                if (obs_time) obs_time->GetEngValue(s, rel_s);
                int64_t epoch_us = static_cast<int64_t>(std::round((start_s + rel_s) * 1e6));

                std::cout << "  [" << s << "] epoch_us=" << epoch_us
                          << " id=0x" << std::hex << raw_id << std::dec
                          << " IDE=" << (int)ide
                          << " SRR=" << (int)srr
                          << " bus=" << (int)bus
                          << " dir=" << (int)dir << "(" << (dir ? "Tx" : "Rx") << ")"
                          << " DLC=" << (int)dlc
                          << " DataLen=" << (int)dlen
                          << " data=[";
                for (size_t i = 0; i < data.size() && i < 8; ++i)
                    std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << " ";
                std::cout << std::dec << "]\n";
            }
        }
    }
    reader.Close();
}

int main()
{
    // Base = 2025-01-01 00:00:00 UTC in nanoseconds
    const uint64_t base_ns = 1735689600ULL * 1000000000ULL;
    const double   base_s  = static_cast<double>(base_ns) * 1e-9;

    std::vector<TestFrame> frames = {
        // standard Rx, bus 0, 5-byte payload
        { base_ns + 0,       0x0CC, false, false, true,  0, {0x10, 0x07, 0x00, 0xFF, 0x35} },
        // extended Rx, bus 0, 8-byte payload
        { base_ns + 9700000, 0x18FF1234, true,  false, true,  0, {0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08} },
        // standard Tx, bus 1, 3-byte payload
        { base_ns + 15000000, 0x123, false, false, false, 1, {0xAA, 0xBB, 0xCC} },
        // NOTE: remote frame removed to test single-CG mandatory-only path
    };

    const std::string out_path = "mdf4_roundtrip_test.mf4";
    std::cout << "=== Saving " << frames.size() << " test frames to " << out_path << " ===\n";
    saveTestFile(out_path, base_ns, frames);
    std::cout << "Save complete.\n";

    std::cout << "\n=== Reading back " << out_path << " ===\n";
    std::cout << "Expected base_s = " << std::fixed << std::setprecision(6) << base_s << "\n";
    readAndPrint(out_path, base_s);

    std::cout << "\n=== Expected values ===\n";
    for (size_t i = 0; i < frames.size(); ++i) {
        const auto& f = frames[i];
        int64_t expected_epoch_us = static_cast<int64_t>(
            std::round(static_cast<double>(f.time_ns) * 1e-3));
        std::cout << "Frame[" << i << "]: epoch_us=" << expected_epoch_us
                  << " id=0x" << std::hex << f.id << std::dec
                  << " ext=" << f.extended
                  << " rx=" << f.rx
                  << " bus=" << f.bus
                  << " data=[";
        for (auto b : f.data) std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)b << " ";
        std::cout << std::dec << "]\n";
    }

    return 0;
}
