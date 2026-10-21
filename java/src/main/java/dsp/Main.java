package dsp;

import dsp.core.Machine;
import dsp.drivers.arcade.Bagman;
import dsp.frontend.AppOptions;
import dsp.frontend.SwingApp;

import java.util.ArrayList;
import java.util.List;

/** Command-line entry point, matching dsp-cpp {@code src/main.cpp} for Bagman. */
public final class Main {
    private Main() {}

    private static final class DipSetting {
        final int bank;
        final int value;

        DipSetting(int bank, int value) {
            this.bank = bank;
            this.value = value;
        }
    }

    public static void main(String[] args) throws Exception {
        AppOptions options = new AppOptions();
        String game = "";
        List<DipSetting> dips = new ArrayList<>();

        for (int index = 0; index < args.length; index++) {
            String argument = args[index];
            if (argument.equals("--help") || argument.equals("-h")) {
                printUsage();
                return;
            } else if (argument.equals("--game")) {
                game = next(args, ++index, "--game");
            } else if (argument.equals("--scale")) {
                options.scale = Integer.parseInt(next(args, ++index, "--scale"));
                if (options.scale < 1) {
                    options.scale = 1;
                }
            } else if (argument.equals("--dip")) {
                String value = next(args, ++index, "--dip");
                int separator = value.indexOf(':');
                if (separator < 0) {
                    dips.add(new DipSetting(0, parseInt(value)));
                } else {
                    dips.add(new DipSetting(Integer.parseInt(value.substring(0, separator)),
                            parseInt(value.substring(separator + 1))));
                }
            } else if (argument.equals("--mute")) {
                options.mute = true;
            } else if (argument.equals("--fullscreen")) {
                options.fullscreen = true;
            } else if (argument.equals("--screenshot")) {
                options.screenshot = next(args, ++index, "--screenshot");
                if (options.frames == 0) {
                    options.frames = 300;
                }
            } else if (argument.equals("--frames")) {
                options.frames = Integer.parseInt(next(args, ++index, "--frames"));
            } else if (!argument.isEmpty() && argument.charAt(0) == '-') {
                System.err.println("unknown option: " + argument);
                System.exit(1);
            } else {
                options.romPath = argument;
            }
        }

        if (game.isEmpty()) {
            System.out.println("jdsp: specify an emulator with --game NAME");
            System.out.println();
            printSupportedEmulators();
            System.out.println("Example: java -jar jdsp.jar --game bagman bagman.zip");
            System.out.println("Use --help for all options.");
            System.exit(1);
        }

        if (options.romPath.isEmpty()) {
            System.err.println("missing ROM path (zip or directory)");
            System.err.println();
            printUsage();
            System.exit(1);
        }

        Machine machine = createMachine(game);
        if (machine == null) {
            System.err.println("unknown game: " + game);
            System.err.println();
            printSupportedEmulators();
            System.exit(1);
        }

        StringBuilder error = new StringBuilder();
        if (!machine.init(options.romPath, error)) {
            System.err.println("cannot start " + game + ": " + error);
            System.exit(1);
        }
        for (DipSetting setting : dips) {
            machine.setDipSwitch(setting.bank, setting.value);
        }
        for (String warning : machine.warnings()) {
            System.err.println("warning: " + warning);
        }

        SwingApp app = new SwingApp(options);
        System.exit(app.run(machine));
    }

    static Machine createMachine(String game) {
        if (game.equals("bagman")) {
            return new Bagman();
        }
        return null;
    }

    private static String next(String[] args, int index, String name) {
        if (index >= args.length) {
            System.err.println(name + " requires a value");
            System.exit(1);
        }
        return args[index];
    }

    private static int parseInt(String value) {
        return Integer.decode(value);
    }

    private static void printSupportedEmulators() {
        System.out.println("Supported emulators (--game NAME):");
        System.out.println();
        System.out.println("  Arcade:");
        System.out.println("    bagman");
        System.out.println();
    }

    private static void printUsage() {
        System.out.println("Usage: java -jar jdsp.jar --game NAME [options] <romset.zip | rom directory>");
        System.out.println();
        printSupportedEmulators();
        System.out.println("Options:");
        System.out.println("  --game NAME        emulator / game to run (required)");
        System.out.println("  --scale N          window scale factor (default 3)");
        System.out.println("  --dip [BANK:]VALUE DIP switch byte, decimal or 0x hex; bagman has one bank");
        System.out.println("  --mute             disable audio");
        System.out.println("  --fullscreen       start maximized");
        System.out.println("  --screenshot FILE  headless mode: render frames and write FILE (BMP)");
        System.out.println("  --frames N         frames to run in headless mode (default 300)");
        System.out.println("  --help             show this help");
        System.out.println();
        System.out.println("Controls: arrows move, Left Ctrl/Space button 1, Left Alt/Z button 2,");
        System.out.println("          1/2 start, 5/6 insert coin, P pause, F3 reset, Esc quit.");
    }
}
