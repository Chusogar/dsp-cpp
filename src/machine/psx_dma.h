// PlayStation DMA controller (7 channels + DPCR/DICR).
// Ported/adapted from ProjectPSX (MIT) by Pedro Cortés:
//   https://github.com/BluestormDNA/ProjectPSX
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace dsp {

// Channel indices: 0 MDECin, 1 MDECout, 2 GPU, 3 CDROM, 4 SPU, 5 PIO, 6 OTC.
class PsxDma {
public:
    // RAM word access (physical, already masked to RAM).
    using RamReader = std::function<uint32_t(uint32_t addr)>;
    using RamWriter = std::function<void(uint32_t addr, uint32_t value)>;
    // Device port transfers (word lists / sized blocks).
    using DeviceFromRam = std::function<void(const uint32_t* data, int words)>;
    using DeviceToRam = std::function<void(uint32_t addr, int words)>;

    void reset();

    void set_ram_callbacks(RamReader reader, RamWriter writer);
    void set_gpu_from_ram(DeviceFromRam cb);
    void set_gpu_to_ram(DeviceToRam cb);
    void set_cdrom_to_ram(DeviceToRam cb);
    void set_spu_from_ram(DeviceFromRam cb);
    void set_spu_to_ram(DeviceToRam cb);
    void set_mdec_from_ram(DeviceFromRam cb);
    void set_mdec_to_ram(DeviceToRam cb);

    uint32_t load(uint32_t addr) const;
    void write(uint32_t addr, uint32_t value);

    // Advances pending sync-mode-1 blocks; returns true if DMA IRQ edge fired.
    bool tick();

private:
    struct Channel {
        uint32_t base = 0;
        uint32_t block_size = 0;
        uint32_t block_count = 0;
        uint32_t direction = 0;   // 0=to RAM, 1=from RAM
        int32_t memory_step = 4;  // +4 or -4
        uint32_t chopping = 0;
        uint32_t sync_mode = 0;
        uint32_t chop_dma_window = 0;
        uint32_t chop_cpu_window = 0;
        bool enable = false;
        bool trigger = false;
        uint32_t unknown29 = 0;
        uint32_t unknown30 = 0;
        uint32_t pending_blocks = 0;

        uint32_t load_control(int channel) const;
        void write_control(int channel, uint32_t value, PsxDma& dma);
    };

    void handle_channel(int channel);
    void finish_channel(int channel);
    void block_copy(int channel, uint32_t size);
    void linked_list(int channel);
    void handle_interrupt(int channel);
    bool update_master_flag() const;
    bool master_enabled(int channel) const;
    bool channel_active(int channel) const;

    Channel channels_[7];
    uint32_t dpcr_ = 0x07654321u;
    bool force_irq_ = false;
    uint32_t irq_enable_ = 0;
    bool master_enable_ = false;
    uint32_t irq_flag_ = 0;
    bool master_flag_ = false;
    bool edge_irq_ = false;

    RamReader ram_read_;
    RamWriter ram_write_;
    DeviceFromRam gpu_from_ram_;
    DeviceToRam gpu_to_ram_;
    DeviceToRam cdrom_to_ram_;
    DeviceFromRam spu_from_ram_;
    DeviceToRam spu_to_ram_;
    DeviceFromRam mdec_from_ram_;
    DeviceToRam mdec_to_ram_;
};

}  // namespace dsp
