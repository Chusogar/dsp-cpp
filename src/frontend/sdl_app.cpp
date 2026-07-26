#include "frontend/sdl_app.h"

#include <SDL.h>

#include <cstdio>
#include <vector>

namespace dsp {
namespace {

constexpr double kFrameTimeMs = 1000.0 / Bagman::kFramesPerSecond;

void collect_inputs(Bagman& machine) {
    const Uint8* keys = SDL_GetKeyboardState(nullptr);
    InputState player1;
    player1.up = keys[SDL_SCANCODE_UP];
    player1.down = keys[SDL_SCANCODE_DOWN];
    player1.left = keys[SDL_SCANCODE_LEFT];
    player1.right = keys[SDL_SCANCODE_RIGHT];
    player1.button = keys[SDL_SCANCODE_LCTRL] || keys[SDL_SCANCODE_SPACE];
    player1.start = keys[SDL_SCANCODE_1];

    InputState player2;
    player2.up = keys[SDL_SCANCODE_R];
    player2.down = keys[SDL_SCANCODE_F];
    player2.left = keys[SDL_SCANCODE_D];
    player2.right = keys[SDL_SCANCODE_G];
    player2.button = keys[SDL_SCANCODE_A];
    player2.start = keys[SDL_SCANCODE_2];

    machine.set_inputs(player1, player2, keys[SDL_SCANCODE_5], keys[SDL_SCANCODE_6]);
}

}  // namespace

int SdlApp::run_headless(Bagman& machine) {
    for (int frame = 0; frame < std::max(options_.frames, 1); frame++) machine.run_frame();

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormatFrom(
        const_cast<uint32_t*>(machine.framebuffer()), Bagman::kScreenWidth, Bagman::kScreenHeight,
        32, Bagman::kScreenWidth * 4, SDL_PIXELFORMAT_ARGB8888);
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

int SdlApp::run(Bagman& machine) {
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

    SDL_Window* window = SDL_CreateWindow(
        "DSP C++ - Bagman", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        Bagman::kScreenWidth * options_.scale, Bagman::kScreenHeight * options_.scale,
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
    SDL_RenderSetLogicalSize(renderer, Bagman::kScreenWidth, Bagman::kScreenHeight);

    SDL_Texture* texture =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                          Bagman::kScreenWidth, Bagman::kScreenHeight);

    SDL_AudioDeviceID audio_device = 0;
    if (!options_.mute) {
        SDL_AudioSpec wanted{};
        wanted.freq = AY8910::kSampleRate;
        wanted.format = AUDIO_S16SYS;
        wanted.channels = 1;
        wanted.samples = 1024;
        audio_device = SDL_OpenAudioDevice(nullptr, 0, &wanted, nullptr, 0);
        if (audio_device != 0) SDL_PauseAudioDevice(audio_device, 0);
    }

    std::vector<int16_t> samples;
    bool running = true;
    bool paused = false;
    uint64_t next_frame = SDL_GetTicks64();

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            if (event.type == SDL_KEYDOWN) {
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE: running = false; break;
                    case SDLK_F3: machine.reset(); break;
                    case SDLK_p: paused = !paused; break;
                    default: break;
                }
            }
        }

        if (!paused) {
            collect_inputs(machine);
            machine.run_frame();

            samples.clear();
            machine.drain_audio(samples);
            if (audio_device != 0 && !samples.empty()) {
                // Drop audio when the queue grows too much (slow machine / paused window).
                if (SDL_GetQueuedAudioSize(audio_device) < AY8910::kSampleRate) {
                    SDL_QueueAudio(audio_device, samples.data(),
                                   uint32_t(samples.size() * sizeof(int16_t)));
                }
            }

            SDL_UpdateTexture(texture, nullptr, machine.framebuffer(), Bagman::kScreenWidth * 4);
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, nullptr, nullptr);
            SDL_RenderPresent(renderer);
        }

        next_frame += uint64_t(kFrameTimeMs);
        uint64_t now = SDL_GetTicks64();
        if (now < next_frame) {
            SDL_Delay(uint32_t(next_frame - now));
        } else {
            next_frame = now;
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
