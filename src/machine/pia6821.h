#pragma once
#include <cstdint>
#include <functional>
namespace dsp {
class Pia6821 {
public:
    using InHandler = std::function<uint8_t()>;
    using OutHandler = std::function<void(uint8_t)>;
    using IrqHandler = std::function<void(bool)>;
    using CbHandler = std::function<void(bool)>;
    Pia6821() { reset(); }
    void reset();
    uint8_t read(uint8_t offset);
    void write(uint8_t offset, uint8_t value);
    void set_in_out(InHandler in_a, InHandler in_b, OutHandler out_a, OutHandler out_b);
    void set_irq(IrqHandler irq_a, IrqHandler irq_b);
    void set_cb2(CbHandler cb2);
    void ca1_w(bool state);
    void cb1_w(bool state);
    void portb_w(uint8_t value);
    bool irq_a_state() const { return irq_a_state_; }
    bool irq_b_state() const { return irq_b_state_; }
private:
    static bool c1_low_to_high(uint8_t c) { return (c & 0x02) != 0; }
    static bool c1_high_to_low(uint8_t c) { return (c & 0x02) == 0; }
    static bool c2_output(uint8_t c) { return (c & 0x20) != 0; }
    static bool c2_input(uint8_t c) { return (c & 0x20) == 0; }
    static bool c2_strobe_mode(uint8_t c) { return (c & 0x30) == 0x20; }
    static bool strobe_c1_reset(uint8_t c) { return (c & 0x08) != 0; }
    static bool output_selected(uint8_t c) { return (c & 0x04) != 0; }
    void update_interrupts();
    void set_out_ca2(bool data);
    void set_out_cb2(bool data);
    uint8_t get_in_a_value() const;
    uint8_t get_in_b_value() const;
    void send_to_out_a();
    void send_to_out_b();
    uint8_t ctl_a_ = 0, ctl_b_ = 0, ddr_a_ = 0, ddr_b_ = 0;
    uint8_t out_a_ = 0, out_b_ = 0, in_a_ = 0, in_b_ = 0;
    bool in_ca1_ = true, in_cb1_ = true, out_ca2_ = false, out_cb2_ = false;
    bool irq_a1_ = false, irq_a2_ = false, irq_b1_ = false, irq_b2_ = false;
    bool irq_a_state_ = false, irq_b_state_ = false;
    InHandler in_a_handler_, in_b_handler_;
    OutHandler out_a_handler_, out_b_handler_;
    IrqHandler irqa_handler_, irqb_handler_;
    CbHandler cb2_handler_;
};
}  // namespace dsp
