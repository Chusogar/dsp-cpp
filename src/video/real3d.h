#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <vector>

namespace dsp {

// Real3D Pro-1000 graphics board of the Sega Model 3 (Step 2.x flavour).
//
// Holds the board's memories (culling RAM, polygon RAM, texture RAM, the
// texture FIFO and VROM), the ping-pong / update-buffer protocol the games
// use to build the next frame while the current one is displayed, the DMA
// engine, the JTAG chain of the ASICs (identification and mode words) and a
// software renderer for the scene graph: viewports, culling nodes with
// matrices, LOD tables and pointer lists, models made of triangles and quads
// with Gouraud / flat / fixed shading, sun lighting, fog, 16 texture formats
// with wrapping / mirroring, translucency and back face culling.
//
// All memories hold "logical" 32-bit words: the value the big-endian CPU
// wrote, byte reversed by the little-endian PCI interface (exactly what the
// board sees). VROM is given as the raw interleaved byte image, read as
// little-endian 32-bit words.
class Real3D {
public:
    static constexpr int kWidth = 496;
    static constexpr int kHeight = 384;

    struct Host {
        std::function<uint32_t(uint32_t)> read32;         // CPU bus (DMA source)
        std::function<void(uint32_t, uint32_t)> write32;  // CPU bus (DMA destination)
        std::function<void(bool)> dma_irq;                // assert / clear DMA IRQ
    };

    Real3D();

    void set_host(Host host) { host_ = std::move(host); }
    // Raw VROM image (interleaved as on the board, little-endian words).
    void set_vrom(const uint8_t* data, size_t size);

    void reset();

    // ---- CPU interface (values already in the board's logical order) ----
    uint32_t read_register(unsigned reg);
    void flush();                                        // 0x88000000
    void write_culling_low(uint32_t offset, uint32_t value);
    void write_culling_high(uint32_t offset, uint32_t value);
    void write_texture_port(uint32_t value);             // 0x90000000
    void write_texture_fifo(uint32_t value);             // 0x94000000
    void write_polygon_ram(uint32_t offset, uint32_t value);
    void write_config(uint32_t reg, uint32_t value) { config_[(reg & 0xf) / 4] = value; }

    uint8_t read_dma8(unsigned reg) const;
    void write_dma8(unsigned reg, uint8_t value);
    uint32_t read_dma32(unsigned reg);
    void write_dma32(unsigned reg, uint32_t value);

    // PCI configuration space read (32-bit, already in CPU order).
    uint32_t pci_config_read(unsigned reg) const;

    // JTAG test access port (system register 0x0C / 0x10).
    void jtag_write(bool tck, bool tms, bool tdi, bool trst);
    bool jtag_tdo() const { return jtag_tdo_; }

    // ---- frame timing (called by the board) ----
    void begin_vblank();
    void flip_ping_pong();
    void tilegen_draw_frame();

    // Renders the 3D scene into an ARGB buffer (alpha 0 = no 3D pixel,
    // colour premultiplied by alpha). render() = prepare() + rasterize():
    // prepare() walks the scene graph in the board memories; rasterize()
    // only needs the prepared polygon lists and texture RAM, so it may run
    // on another thread while the CPU builds the next frame.
    void render(std::vector<uint32_t>& out);
    void prepare();
    void rasterize(std::vector<uint32_t>& out);
    // Called before texture RAM is modified (lets a renderer thread finish).
    // Number of horizontal bands rasterized in parallel (1 = no threads).
    void set_render_threads(int n) { band_count_ = n < 1 ? 1 : n; }
    void set_texture_write_hook(std::function<void()> hook) { before_texture_write_ = std::move(hook); }
    bool frame_ready() const { return frame_ready_; }

    // Debug / test helpers.
    uint32_t culling_low(uint32_t word) const { return cull_lo_[word & 0xfffff]; }
    uint32_t culling_high(uint32_t word) const { return cull_hi_[word & 0x3ffff]; }
    uint32_t polygon(uint32_t word) const { return poly_[word & 0xfffff]; }
    uint16_t texel(uint32_t x, uint32_t y) const { return tex_[(y & 2047) * 2048 + (x & 2047)]; }
    uint32_t modeword(int asic) const { return modeword_[size_t(asic) % 5]; }
    struct ViewportInfo {
        float x, y, w, h, l, r, b, t;
        int priority;
    };
    std::vector<ViewportInfo> viewport_info() const {
        std::vector<ViewportInfo> v;
        for (const auto& s : viewports_) v.push_back({s.x, s.y, s.w, s.h, s.l, s.r, s.b, s.t, s.priority});
        return v;
    }
    int last_polygon_count() const { return stats_polys_; }
    int last_viewport_count() const { return stats_viewports_; }
    int texture_uploads() const { return stats_uploads_; }

    // Debug accessors used by tests (render a single model with a matrix).
    void upload_texture(uint32_t header, const uint16_t* data);

private:
    // ---- memory helpers ----
    const uint32_t* cull_ptr(uint32_t addr) const;
    const uint32_t* model_ptr(uint32_t addr) const;
    uint32_t vrom_word(uint32_t index) const;
    bool ping_pong_flipped() const { return ping_pong_ != ping_pong_copy_; }
    void flush_textures();
    void draw_frame();
    void store_texture(unsigned x, unsigned y, unsigned width, unsigned height, const uint16_t* data,
                       bool sixteen_bit, bool write_lsb, bool write_msb, uint32_t& consumed);

    // ---- JTAG ----
    struct JtagDevice {
        bool asic = false;
        int asic_index = -1;  // 0 mercury .. 4 jupiter (modeword slot)
        uint32_t idcode = 0;
        uint8_t ir = 0;
        int size = 1;
        uint64_t shift = 0;
    };
    void jtag_reset_devices();

    // ---- renderer ----
    struct Mat4 {
        float m[16];  // column major (OpenGL)
    };
    struct Vtx {
        float x, y, z;     // view space
        float nx, ny, nz;  // view space normal
        float u, v;
        float shade;       // fixed shading value
    };
    struct Poly {
        float r = 1, g = 1, b = 1, a = 1;  // face colour and alpha (incl. node alpha)
        bool textured = false;
        int tx = 0, ty = 0, tw = 32, th = 32, page = 0, format = 0;
        bool mirror_u = false, mirror_v = false;
        bool alpha_test = false, texture_alpha = false, poly_alpha = false;
        bool node_alpha = false;
        bool lighting = true, fixed_shading = false;
        bool high_priority = false;
        bool inverted = false;  // texture colour inverted (translator map offset 2)
        float fog_intensity = 0;
        int vp = 0;  // viewport index
    };
    struct ViewportState {
        float x, y, w, h;
        float l, r, b, t;
        float sun[3], sun_intensity, ambient;
        float fog_rgb[3], fog_density, fog_start, fog_ambient, scroll_fog;
        int priority;
    };
    struct Deferred {
        Poly p;
        Vtx v[4];
        int n;
    };
    void render_viewport(uint32_t addr, int priority, int depth);
    void descend_node(uint32_t addr, int depth);
    void descend_ptr(uint32_t addr, int depth);
    void descend_list(uint32_t addr, int depth);
    void draw_model(uint32_t addr);
    void mult_matrix(uint32_t index);
    void reset_matrix_rotation();
    void raster_poly(const Vtx* v, int n, const Poly& p, bool translucent, int band_y0, int band_y1);

    Host host_;
    std::vector<uint32_t> cull_lo_;   // 1M words
    std::vector<uint32_t> cull_hi_;   // 256K words
    std::vector<uint32_t> poly_;      // 1M words
    std::vector<uint16_t> tex_;       // 2048 x 2048 texels (two 2048x1024 pages)
    std::vector<uint32_t> fifo_;      // texture FIFO
    const uint8_t* vrom_ = nullptr;
    size_t vrom_words_ = 0;

    struct PendingWrite {
        uint32_t index;
        uint32_t value;
    };
    std::vector<PendingWrite> pending_hi_, pending_poly_;
    std::array<uint32_t, 4> config_{};
    std::array<uint32_t, 2> vrom_fifo_{};
    int vrom_fifo_idx_ = 0;

    bool ping_pong_ = false;
    bool ping_pong_copy_ = false;
    bool command_written_ = false;
    bool tilegen_draw_ = false;
    bool frame_ready_ = false;
    bool block_culling_ = false;

    uint32_t dma_src_ = 0, dma_dst_ = 0, dma_len_ = 0, dma_data_ = 0;
    uint8_t dma_status_ = 0, dma_config_ = 0;

    std::array<JtagDevice, 17> jtag_{};
    std::array<uint32_t, 5> modeword_{};
    int jtag_state_ = 0;
    bool jtag_last_tck_ = false;
    bool jtag_tdo_ = false;

    // renderer state
    std::vector<float> zbuf_;
    std::vector<uint32_t> colour_;
    Mat4 mat_{};
    std::vector<Mat4> mat_stack_;
    const uint32_t* matrix_base_ = nullptr;
    const uint32_t* lod_table_ = nullptr;
    uint32_t color_table_ = 0;
    float model_scale_ = 1.0f;
    float model_alpha_ = 1.0f;
    bool disable_culling_ = false;
    int tex_off_x_ = 0, tex_off_y_ = 0, tex_page_ = 0;
    std::array<float, 8> planes_{};
    bool sun_clamp_ = true;
    bool prepared_blocked_ = false;
    std::function<void()> before_texture_write_;
    int band_count_ = 1;
    std::vector<ViewportState> viewports_;
    std::array<std::vector<Deferred>, 4> frame_polys_;
    int stats_polys_ = 0, stats_viewports_ = 0, stats_uploads_ = 0;
    int cur_polys_ = 0;
};

}  // namespace dsp
