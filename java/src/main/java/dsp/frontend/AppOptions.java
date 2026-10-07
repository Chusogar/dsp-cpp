package dsp.frontend;

/** Front-end options, matching dsp-cpp {@code frontend/sdl_app.h}. */
public final class AppOptions {
    public String romPath = "";
    public int scale = 3;
    public boolean mute;
    public boolean fullscreen;
    public String screenshot = "";
    public int frames;
}
