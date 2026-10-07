// PlayStation DMA controller.
// Ported/adapted from ProjectPSX (MIT) by Pedro Cortés:
//   https://github.com/BluestormDNA/ProjectPSX
#include "machine/psx_dma.h"

#include <algorithm>

namespace dsp {

void PsxDma::reset() {
    for (auto& ch : channels_) {
        ch = Channel{};
    }
    dpcr_ = 0x07654321u;
    force_irq_ = false;
    irq_enable_ = 0;
    master_enable_ = false;
    irq_flag_ = 0;
    master_flag_ = false;
    edge_irq_ = false;
}

void PsxDma::set_ram_callbacks(RamReader reader, RamWriter writer) {
    ram_read_ = std::move(reader);
    ram_write_ = std::move(writer);
}
void PsxDma::set_gpu_from_ram(DeviceFromRam cb) { gpu_from_ram_ = std::move(cb); }
void PsxDma::set_gpu_to_ram(DeviceToRam cb) { gpu_to_ram_ = std::move(cb); }
void PsxDma::set_cdrom_to_ram(DeviceToRam cb) { cdrom_to_ram_ = std::move(cb); }
void PsxDma::set_spu_from_ram(DeviceFromRam cb) { spu_from_ram_ = std::move(cb); }
void PsxDma::set_spu_to_ram(DeviceToRam cb) { spu_to_ram_ = std::move(cb); }
void PsxDma::set_mdec_from_ram(DeviceFromRam cb) { mdec_from_ram_ = std::move(cb); }
void PsxDma::set_mdec_to_ram(DeviceToRam cb) { mdec_to_ram_ = std::move(cb); }

uint32_t PsxDma::Channel::load_control(int channel) const {
    uint32_t ctrl = 0;
    ctrl |= direction;
    ctrl |= (memory_step == 4 ? 0u : 1u) << 1;
    ctrl |= chopping << 8;
    ctrl |= sync_mode << 9;
    ctrl |= chop_dma_window << 16;
    ctrl |= chop_cpu_window << 20;
    ctrl |= (enable ? 1u : 0u) << 24;
    ctrl |= (trigger ? 1u : 0u) << 28;
    ctrl |= unknown29 << 29;
    ctrl |= unknown30 << 30;
    if (channel == 6) {
        return (ctrl & 0x50000002u) | 0x2u;
    }
    return ctrl;
}

void PsxDma::Channel::write_control(int channel, uint32_t value, PsxDma& dma) {
    direction = value & 1u;
    memory_step = ((value >> 1) & 1u) == 0 ? 4 : -4;
    chopping = (value >> 8) & 1u;
    sync_mode = (value >> 9) & 3u;
    chop_dma_window = (value >> 16) & 7u;
    chop_cpu_window = (value >> 20) & 7u;
    enable = ((value >> 24) & 1u) != 0;
    trigger = ((value >> 28) & 1u) != 0;
    unknown29 = (value >> 29) & 1u;
    unknown30 = (value >> 30) & 1u;
    if (!enable) pending_blocks = 0;
    dma.handle_channel(channel);
}

uint32_t PsxDma::load(uint32_t addr) const {
    const int channel = int((addr & 0x70) >> 4);
    const uint32_t reg = addr & 0xF;
    if (channel < 7) {
        const Channel& ch = channels_[channel];
        switch (reg) {
            case 0: return ch.base;
            case 4: return (ch.block_count << 16) | ch.block_size;
            case 8: return ch.load_control(channel);
            default: return 0;
        }
    }
    // Interrupt / DPCR channel (7)
    if (reg == 0) return dpcr_;
    if (reg == 4 || reg == 6) {
        uint32_t irq = 0;
        irq |= (force_irq_ ? 1u : 0u) << 15;
        irq |= irq_enable_ << 16;
        irq |= (master_enable_ ? 1u : 0u) << 23;
        irq |= irq_flag_ << 24;
        irq |= (master_flag_ ? 1u : 0u) << 31;
        return reg == 6 ? (irq >> 16) : irq;
    }
    return 0xFFFFFFFFu;
}

void PsxDma::write(uint32_t addr, uint32_t value) {
    const int channel = int((addr & 0x70) >> 4);
    const uint32_t reg = addr & 0xF;
    if (channel < 7) {
        Channel& ch = channels_[channel];
        switch (reg) {
            case 0: ch.base = value & 0xFFFFFFu; break;
            case 4:
                ch.block_count = value >> 16;
                ch.block_size = value & 0xFFFFu;
                break;
            case 8: ch.write_control(channel, value, *this); break;
            default: break;
        }
        return;
    }
    if (reg == 0) {
        dpcr_ = value;
    } else if (reg == 4 || reg == 6) {
        uint32_t v = value;
        if (reg == 6) v = (value << 16) | ((force_irq_ ? 1u : 0u) << 15);
        force_irq_ = ((v >> 15) & 1u) != 0;
        irq_enable_ = (v >> 16) & 0x7Fu;
        master_enable_ = ((v >> 23) & 1u) != 0;
        irq_flag_ &= ~((v >> 24) & 0x7Fu);
        master_flag_ = update_master_flag();
    }
}

bool PsxDma::tick() {
    for (int i = 0; i < 7; i++) {
        Channel& ch = channels_[i];
        if (ch.pending_blocks > 0) {
            ch.pending_blocks--;
            block_copy(i, ch.block_size);
            if (ch.pending_blocks == 0) finish_channel(i);
        }
    }
    if (edge_irq_) {
        edge_irq_ = false;
        return true;
    }
    return false;
}

bool PsxDma::master_enabled(int channel) const {
    return ((dpcr_ >> 3 >> (4 * channel)) & 1u) != 0;
}

bool PsxDma::channel_active(int channel) const {
    const Channel& ch = channels_[channel];
    return ch.sync_mode == 0 ? (ch.enable && ch.trigger) : ch.enable;
}

bool PsxDma::update_master_flag() const {
    return force_irq_ || (master_enable_ && (irq_enable_ & irq_flag_) != 0);
}

void PsxDma::handle_interrupt(int channel) {
    if ((irq_enable_ & (1u << channel)) != 0) {
        irq_flag_ |= 1u << channel;
    }
    master_flag_ = update_master_flag();
    edge_irq_ = edge_irq_ || master_flag_;
}

void PsxDma::finish_channel(int channel) {
    Channel& ch = channels_[channel];
    ch.enable = false;
    ch.trigger = false;
    handle_interrupt(channel);
}

void PsxDma::handle_channel(int channel) {
    if (!channel_active(channel) || !master_enabled(channel)) return;
    Channel& ch = channels_[channel];
    if (ch.sync_mode == 0) {
        block_copy(channel, ch.block_size == 0 ? 0x10000u : ch.block_size);
        finish_channel(channel);
    } else if (ch.sync_mode == 1) {
        // GPU-in / MDECin: transfer all at once (ProjectPSX hack).
        if ((channel == 2 && ch.direction == 1) || channel == 0) {
            block_copy(channel, ch.block_size * ch.block_count);
            finish_channel(channel);
            return;
        }
        ch.trigger = false;
        ch.pending_blocks = ch.block_count;
    } else if (ch.sync_mode == 2) {
        linked_list(channel);
        finish_channel(channel);
    }
}

void PsxDma::block_copy(int channel, uint32_t size) {
    Channel& ch = channels_[channel];
    if (ch.direction == 0) {
        // To RAM
        switch (channel) {
            case 1:
                if (mdec_to_ram_) mdec_to_ram_(ch.base, int(size));
                break;
            case 2:
                if (gpu_to_ram_) gpu_to_ram_(ch.base, int(size));
                break;
            case 3:
                if (cdrom_to_ram_) cdrom_to_ram_(ch.base, int(size));
                break;
            case 4:
                if (spu_to_ram_) spu_to_ram_(ch.base, int(size));
                break;
            case 6: {
                // OTC: write linked list of decreasing addresses.
                uint32_t addr = ch.base;
                for (uint32_t i = 0; i + 1 < size; i++) {
                    if (ram_write_) ram_write_(addr & 0x1FFFFCu, (addr - 4) & 0xFFFFFFu);
                    addr -= 4;
                }
                if (ram_write_) ram_write_(addr & 0x1FFFFCu, 0x00FFFFFFu);
                break;
            }
            default:
                break;
        }
        ch.base += uint32_t(ch.memory_step) * size;
    } else {
        // From RAM
        std::vector<uint32_t> words(size);
        uint32_t addr = ch.base & 0x1FFFFCu;
        for (uint32_t i = 0; i < size; i++) {
            words[i] = ram_read_ ? ram_read_(addr) : 0;
            addr = (addr + 4) & 0x1FFFFFu;
        }
        switch (channel) {
            case 0:
                if (mdec_from_ram_) mdec_from_ram_(words.data(), int(size));
                break;
            case 2:
                if (gpu_from_ram_) gpu_from_ram_(words.data(), int(size));
                break;
            case 4:
                if (spu_from_ram_) spu_from_ram_(words.data(), int(size));
                break;
            default:
                break;
        }
        ch.base += uint32_t(ch.memory_step) * size;
    }
}

void PsxDma::linked_list(int channel) {
    Channel& gpu_ch = channels_[channel];
    uint32_t header = 0;
    uint32_t hard_stop = 0xFFFFu;
    while ((header & 0x800000u) == 0 && hard_stop-- != 0) {
        header = ram_read_ ? ram_read_(gpu_ch.base & 0x1FFFFCu) : 0;
        const uint32_t size = header >> 24;
        if (size > 0) {
            gpu_ch.base = (gpu_ch.base + 4) & 0x1FFFFCu;
            std::vector<uint32_t> words(size);
            uint32_t addr = gpu_ch.base;
            for (uint32_t i = 0; i < size; i++) {
                words[i] = ram_read_ ? ram_read_(addr) : 0;
                addr = (addr + 4) & 0x1FFFFFu;
            }
            if (gpu_from_ram_) gpu_from_ram_(words.data(), int(size));
        }
        if (gpu_ch.base == (header & 0x1FFFFCu)) break;
        gpu_ch.base = header & 0x1FFFFCu;
    }
}

}  // namespace dsp
