#include "frontend/sdl_app.h"

#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace dsp {
namespace {

// Host scancode of every Key the machines can read.
constexpr struct {
    Key key;
    SDL_Scancode scancode;
} kKeyMap[] = {
    {Key::A, SDL_SCANCODE_A},         {Key::B, SDL_SCANCODE_B},
    {Key::C, SDL_SCANCODE_C},         {Key::D, SDL_SCANCODE_D},
    {Key::E, SDL_SCANCODE_E},         {Key::F, SDL_SCANCODE_F},
    {Key::G, SDL_SCANCODE_G},         {Key::H, SDL_SCANCODE_H},
    {Key::I, SDL_SCANCODE_I},         {Key::J, SDL_SCANCODE_J},
    {Key::K, SDL_SCANCODE_K},         {Key::L, SDL_SCANCODE_L},
    {Key::M, SDL_SCANCODE_M},         {Key::N, SDL_SCANCODE_N},
    {Key::O, SDL_SCANCODE_O},         {Key::P, SDL_SCANCODE_P},
    {Key::Q, SDL_SCANCODE_Q},         {Key::R, SDL_SCANCODE_R},
    {Key::S, SDL_SCANCODE_S},         {Key::T, SDL_SCANCODE_T},
    {Key::U, SDL_SCANCODE_U},         {Key::V, SDL_SCANCODE_V},
    {Key::W, SDL_SCANCODE_W},         {Key::X, SDL_SCANCODE_X},
    {Key::Y, SDL_SCANCODE_Y},         {Key::Z, SDL_SCANCODE_Z},
    {Key::Num0, SDL_SCANCODE_0},      {Key::Num1, SDL_SCANCODE_1},
    {Key::Num2, SDL_SCANCODE_2},      {Key::Num3, SDL_SCANCODE_3},
    {Key::Num4, SDL_SCANCODE_4},      {Key::Num5, SDL_SCANCODE_5},
    {Key::Num6, SDL_SCANCODE_6},      {Key::Num7, SDL_SCANCODE_7},
    {Key::Num8, SDL_SCANCODE_8},      {Key::Num9, SDL_SCANCODE_9},
    {Key::Enter, SDL_SCANCODE_RETURN}, {Key::Space, SDL_SCANCODE_SPACE},
    {Key::LeftShift, SDL_SCANCODE_LSHIFT}, {Key::RightShift, SDL_SCANCODE_RSHIFT},
    {Key::LeftCtrl, SDL_SCANCODE_LCTRL}, {Key::RightCtrl, SDL_SCANCODE_RCTRL},
    {Key::Backspace, SDL_SCANCODE_BACKSPACE}, {Key::Up, SDL_SCANCODE_UP},
    {Key::Down, SDL_SCANCODE_DOWN},   {Key::Left, SDL_SCANCODE_LEFT},
    {Key::Right, SDL_SCANCODE_RIGHT}, {Key::Comma, SDL_SCANCODE_COMMA},
    {Key::Period, SDL_SCANCODE_PERIOD}, {Key::Semicolon, SDL_SCANCODE_SEMICOLON},
    {Key::Quote, SDL_SCANCODE_APOSTROPHE}, {Key::Slash, SDL_SCANCODE_SLASH},
    {Key::Minus, SDL_SCANCODE_MINUS},
};

void collect_inputs(Machine& machine) {
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    MachineInputs inputs;
    inputs.player1.up = keys[SDL_SCANCODE_UP];
    inputs.player1.down = keys[SDL_SCANCODE_DOWN];
    inputs.player1.left = keys[SDL_SCANCODE_LEFT];
    inputs.player1.right = keys[SDL_SCANCODE_RIGHT];
    inputs.player1.button1 = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_SPACE];
    inputs.player1.button2 = keys[SDL_SCANCODE_LALT] || keys[SDL_SCANCODE_Z];
    inputs.player1.button3 = keys[SDL_SCANCODE_X];
    inputs.player1.start = keys[SDL_SCANCODE_1];

    inputs.player2.up = keys[SDL_SCANCODE_R];
    inputs.player2.down = keys[SDL_SCANCODE_F];
    inputs.player2.left = keys[SDL_SCANCODE_D];
    inputs.player2.right = keys[SDL_SCANCODE_G];
    inputs.player2.button1 = keys[SDL_SCANCODE_A];
    inputs.player2.button2 = keys[SDL_SCANCODE_S];
    inputs.player2.button3 = keys[SDL_SCANCODE_Q];
    inputs.player2.start = keys[SDL_SCANCODE_2];

    inputs.coin1 = keys[SDL_SCANCODE_5];
    inputs.coin2 = keys[SDL_SCANCODE_6];

    if (machine.uses_keyboard()) {
        for (const auto& entry : kKeyMap) inputs.keys[size_t(entry.key)] = keys[entry.scancode];
    }
    machine.set_inputs(inputs);
}

}  // namespace

int SdlApp::run_headless(Machine& machine) {
    for (int frame = 0; frame < std::max(options_.frames, 1); frame++) machine.run_frame();

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(
        const_cast<uint32_t*>(machine.framebuffer()), machine.screen_width(),
        machine.screen_height(), 32, machine.screen_width() * 4, SDL_PIXELFORMAT_ARGB8888);
    if (surface == nullptr) {
        std::fprintf(stderr, "cannot create surface: %s\n", SDL_GetError());
        return 1;
    }
    int result = SDL_SaveBMP(surface, options_.screenshot.c_str());
    SDL_FreeSurface(surface);
    if (result != 0) {
        std::fprintf(stderr, "cannot save %s: %s\n", options_.screenshot.c_str(), SDL_GetError());
        return 1;
    }
    std::printf("wrote %s\n", options_.screenshot.c_str());
    return 0;
}

int SdlApp::run(Machine& machine) {
    if (!options_.screenshot.empty()) {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            return 1;
        }
        int result = run_headless(machine);
        SDL_Quit();
        return result;
    }

    uint32_t flags = SDL_INIT_VIDEO | SDL_INIT_EVENTS;
    if (!options_.mute) flags |= SDL_INIT_AUDIO;
    if (SDL_Init(flags) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    const int width = machine.screen_width();
    const int height = machine.screen_height();
    const double frame_time_ms = 1000.0 / machine.frames_per_second();
    const std::string title = std::string("DSP C++ - ") + machine.title();

    SDL_Window* window =
        SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                         width * options_.scale, height * options_.scale,
                         options_.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
    if (window == nullptr) {
        std::fprintf(stderr, "cannot create window: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (renderer == nullptr) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
    if (renderer == nullptr) {
        std::fprintf(stderr, "cannot create renderer: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    SDL_RenderSetLogicalSize(renderer, width, height);

    SDL_Texture* texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                             SDL_TEXTUREACCESS_STREAMING, width, height);

    SDL_AudioDeviceID audio_device = 0;
    const int sample_rate = machine.sample_rate();
    // Target \~3 frames of audio latency; keep queue between \~1.5 and \~4.5 frames.
    // Video is paced by waiting when the SDL audio queue exceeds max_bytes, so
    // the presentation clock stays locked to the audio clock and does not drift.
    const double samples_per_frame = double(sample_rate) / machine.frames_per_second();
    const uint32_t max_bytes = uint32_t(samples_per_frame * 4.5 * sizeof(int16_t));
    const uint32_t min_bytes = uint32_t(samples_per_frame * 1.5 * sizeof(int16_t));

    if (!options_.mute) {
        SDL_AudioSpec wanted{};
        wanted.freq = sample_rate;
        wanted.format = AUDIO_S16SYS;
        wanted.channels = 1;
        // Smaller callback buffer reduces latency; SDL still queues behind it.
        wanted.samples = 512;
        audio_device = SDL_OpenAudioDevice(nullptr, 0, &wanted, nullptr, 0);
        if (audio_device != 0) SDL_PauseAudioDevice(audio_device, 0);
    }

    std::vector<int16_t> samples;
    bool running = true;
    bool paused = false;
    // Accumulator for sub-ms frame pacing when muted (avoids integer truncation drift).
    double frame_debt_ms = 0.0;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_KEYDOWN) {
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE: running = false; break;
                    case SDLK_F3: machine.reset(); break;
                    case SDLK_F2: paused = !paused; break;
                    case SDLK_p:
                        if (!machine.uses_keyboard()) paused = !paused;
                        break;
                    default: break;
                }
            }
        }

        if (paused) {
            // Drain a little while paused so the queue does not go silent forever
            // and so we do not spin at 100% CPU.
            SDL_Delay(10);
            continue;
        }

        // --- Audio-master pacing when possible ---
        // If the queue is already full enough, wait until it drains toward the
        // target. This keeps video locked to the audio clock and prevents
        // cumulative drift.
        if (audio_device != 0) {
            for (;;) {
                const uint32_t queued = SDL_GetQueuedAudioSize(audio_device);
                if (queued < max_bytes) break;
                // Sleep a fraction of a frame; re-check after events.
                SDL_Delay(1);
                // Process quit / pause quickly while waiting.
                if (SDL_PollEvent(&event)) {
                    if (event.type == SDL_QUIT) {
                        running = false;
                        break;
                    }
                    if (event.type == SDL_KEYDOWN) {
                        if (event.key.keysym.sym == SDLK_ESCAPE) running = false;
                        if (event.key.keysym.sym == SDLK_F2 ||
                            (event.key.keysym.sym == SDLK_p && !machine.uses_keyboard())) {
                            paused = true;
                        }
                        if (event.key.keysym.sym == SDLK_F3) machine.reset();
                    }
                }
                if (!running || paused) break;
            }
            if (!running || paused) continue;
        }

        collect_inputs(machine);
        machine.run_frame();

        samples.clear();
        machine.drain_audio(samples);
        if (audio_device != 0 && !samples.empty()) {
            // Soft drop only if the queue is pathologically large (host too
            // slow). Prefer waiting (above) over discarding samples so pitch
            // and A/V stay locked.
            if (SDL_GetQueuedAudioSize(audio_device) < uint32_t(sample_rate) * 2) {
                SDL_QueueAudio(audio_device, samples.data(),
                               uint32_t(samples.size() * sizeof(int16_t)));
            }
        }

        SDL_UpdateTexture(texture, nullptr, machine.framebuffer(), width * 4);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);

        // --- Fallback / fine pacing when muted or queue is under-filled ---
        if (audio_device == 0) {
            // Classic wall-clock limiter with fractional accumulation to avoid
            // long-term drift from integer millisecond truncation.
            frame_debt_ms += frame_time_ms;
            const int delay_ms = int(frame_debt_ms);
            if (delay_ms > 0) {
                SDL_Delay(uint32_t(delay_ms));
                frame_debt_ms -= delay_ms;
            }
            // If we are behind, do not accumulate infinite debt.
            if (frame_debt_ms < -frame_time_ms * 2.0) frame_debt_ms = 0.0;
        } else {
            // With audio open: if the queue is below the soft minimum, run the
            // next frame immediately (no delay) so we refill. Otherwise sleep
            // a tiny amount to yield CPU; the audio-wait loop at the top of
            // the next iteration will do the real pacing.
            const uint32_t queued = SDL_GetQueuedAudioSize(audio_device);
            if (queued >= min_bytes) {
                // Aim for roughly one frame of headroom without busy-waiting.
                SDL_Delay(1);
            }
        }
    }

    if (audio_device != 0) SDL_CloseAudioDevice(audio_device);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

}  // namespace dsp

