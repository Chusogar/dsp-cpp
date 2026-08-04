#include "frontend/sdl_app.h"

#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace dsp {
namespace {

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
    if (!options_.mute) {
        SDL_AudioSpec wanted{};
        wanted.freq = machine.sample_rate();
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
                if (SDL_GetQueuedAudioSize(audio_device) < uint32_t(machine.sample_rate())) {
                    SDL_QueueAudio(audio_device, samples.data(),
                                   uint32_t(samples.size() * sizeof(int16_t)));
                }
            }

            SDL_UpdateTexture(texture, nullptr, machine.framebuffer(), width * 4);
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, nullptr, nullptr);
            SDL_RenderPresent(renderer);
        }

        next_frame += uint64_t(frame_time_ms);
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
