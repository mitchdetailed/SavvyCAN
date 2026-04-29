// MDF4 sample value inspector - dumps first N samples per CAN CG
// Build: g++ -std=c++17 -I../third_party/mdflib/include mdf4_inspect.cpp -L../third_party/mdflib/build/mdflib -lmdf -L"C:/Program Files/mingw64/x86_64-w64-mingw32/lib" -lz -L"C:/Program Files/mingw64/opt/lib" -lexpat -o mdf4_inspect.exe
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <vector>
#include <mdf/mdfreader.h>
#include <mdf/mdffile.h>
#include <mdf/idatagroup.h>
#include <mdf/ichannelgroup.h>
#include <mdf/ichannel.h>
#include <mdf/ichannelobserver.h>

static const int DUMP_SAMPLES = 5;

int main(int argc, char* argv[])
{
    if (argc < 2) { std::cerr << "Usage: mdf4_inspect <file.mf4>\n"; return 1; }

    mdf::MdfReader reader(argv[1]);
    reader.ReadEverythingButData();

    const auto* f = reader.GetFile();
    if (!f) { std::cerr << "Failed to open file\n"; return 1; }

    mdf::DataGroupList dg_list;
    f->DataGroups(dg_list);

    for (size_t di = 0; di < dg_list.size(); ++di) {
        auto* dg = dg_list[di];

        struct Bundle {
            mdf::IChannelGroup* cg;
            std::unique_ptr<mdf::IChannelObserver> obs_time;
            std::unique_ptr<mdf::IChannelObserver> obs_id;
            std::unique_ptr<mdf::IChannelObserver> obs_ide;
            std::unique_ptr<mdf::IChannelObserver> obs_bus;
            std::unique_ptr<mdf::IChannelObserver> obs_data;
            std::unique_ptr<mdf::IChannelObserver> obs_datalen;
        };
        std::vector<Bundle> bundles;

        for (auto* cg : dg->ChannelGroups()) {
            auto* cn_id = cg->GetChannel("CAN_DataFrame.ID");
            if (!cn_id) cn_id = cg->GetChannel("CAN_RemoteFrame.ID");
            if (!cn_id) continue;

            Bundle b;
            b.cg = cg;
            auto* cn_time    = cg->GetMasterChannel();
            auto* cn_ide     = cg->GetChannel(".IDE");
            auto* cn_bus     = cg->GetChannel(".BusChannel");
            auto* cn_data    = cg->GetChannel(".DataBytes");
            auto* cn_datalen = cg->GetChannel(".DataLength");

            if (cn_time)    b.obs_time    = mdf::CreateChannelObserver(*dg, *cg, *cn_time);
            b.obs_id                      = mdf::CreateChannelObserver(*dg, *cg, *cn_id);
            if (cn_ide)     b.obs_ide     = mdf::CreateChannelObserver(*dg, *cg, *cn_ide);
            if (cn_bus)     b.obs_bus     = mdf::CreateChannelObserver(*dg, *cg, *cn_bus);
            if (cn_data)    b.obs_data    = mdf::CreateChannelObserver(*dg, *cg, *cn_data);
            if (cn_datalen) b.obs_datalen = mdf::CreateChannelObserver(*dg, *cg, *cn_datalen);
            bundles.push_back(std::move(b));
        }

        if (bundles.empty()) continue;
        reader.ReadData(*dg);

        for (auto& b : bundles) {
            uint64_t n = b.obs_id->NofSamples();
            if (n == 0) continue;

            // Print master channel metadata
            std::string time_unit, time_name;
            mdf::IChannel* master_cn = b.cg->GetMasterChannel();
            if (master_cn) {
                time_name = master_cn->Name();
                time_unit = master_cn->Unit();
            }

            std::cout << "\nCG '" << b.cg->Name() << "' -> " << n << " samples\n";
            std::cout << "  Master channel: '" << time_name << "' unit='" << time_unit << "'\n";

            uint64_t limit = (n < DUMP_SAMPLES) ? n : DUMP_SAMPLES;
            for (uint64_t s = 0; s < limit; ++s) {
                double   ts_double = 0.0;
                uint64_t ts_uint64 = 0;
                uint32_t raw_id = 0;
                uint8_t  ide = 0xff;
                uint8_t  bus = 0;
                uint8_t  datalen = 0;
                std::vector<uint8_t> data;

                if (b.obs_time) {
                    b.obs_time->GetChannelValue(s, ts_double);
                    b.obs_time->GetChannelValue(s, ts_uint64);
                    double ts_eng = 0.0;
                    b.obs_time->GetEngValue(s, ts_eng);
                    std::cout << "  [" << s << "] ts_raw=" << ts_uint64
                              << " ts_eng(s)=" << std::fixed << std::setprecision(9) << ts_eng;
                } else {
                    std::cout << "  [" << s << "] ts=N/A";
                }
                b.obs_id->GetChannelValue(s, raw_id);
                if (b.obs_ide)     b.obs_ide->GetChannelValue(s, ide);
                if (b.obs_bus)     b.obs_bus->GetChannelValue(s, bus);
                if (b.obs_datalen) b.obs_datalen->GetChannelValue(s, datalen);
                if (b.obs_data)    b.obs_data->GetChannelValue(s, data);

                std::cout << " raw_id=0x" << std::hex << raw_id << std::dec
                          << " IDE=" << (int)ide
                          << " bus=" << (int)bus
                          << " datalen=" << (int)datalen
                          << " data=[";
                for (size_t i = 0; i < data.size() && i < 8; ++i)
                    std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)data[i] << " ";
                std::cout << std::dec << "]\n";
            }

            if (b.obs_time)    b.obs_time->DetachObserver();
            b.obs_id->DetachObserver();
            if (b.obs_ide)     b.obs_ide->DetachObserver();
            if (b.obs_bus)     b.obs_bus->DetachObserver();
            if (b.obs_data)    b.obs_data->DetachObserver();
            if (b.obs_datalen) b.obs_datalen->DetachObserver();
        }
    }

    reader.Close();
    return 0;
}
