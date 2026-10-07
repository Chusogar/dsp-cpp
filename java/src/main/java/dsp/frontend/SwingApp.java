package dsp.frontend;

import dsp.core.Key;
import dsp.core.Machine;
import dsp.core.MachineInputs;

import javax.sound.sampled.AudioFormat;
import javax.sound.sampled.AudioSystem;
import javax.sound.sampled.DataLine;
import javax.sound.sampled.SourceDataLine;
import javax.swing.JFrame;
import javax.swing.JPanel;
import javax.swing.SwingUtilities;
import javax.swing.Timer;
import javax.swing.WindowConstants;
import java.awt.Color;
import java.awt.Dimension;
import java.awt.Graphics;
import java.awt.Graphics2D;
import java.awt.KeyboardFocusManager;
import java.awt.RenderingHints;
import java.awt.event.KeyAdapter;
import java.awt.event.KeyEvent;
import java.awt.image.BufferedImage;
import java.awt.image.DataBufferInt;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Swing + Java Sound front end: window, nearest-neighbour blit, audio queue and keyboard.
 * Ported from dsp-cpp {@code frontend/sdl_app.cpp}.
 */
public final class SwingApp {
    private final AppOptions options;
    private volatile boolean running = true;
    private volatile boolean paused;
    private final Set<Integer> keysDown = ConcurrentHashMap.newKeySet();

    public SwingApp(AppOptions options) {
        this.options = options;
    }

    public int run(Machine machine) throws Exception {
        if (options.screenshot != null && !options.screenshot.isEmpty()) {
            return runHeadless(machine);
        }
        return runWindow(machine);
    }

    private int runHeadless(Machine machine) throws Exception {
        int frames = Math.max(options.frames, 1);
        int coinFrom = envInt("JDSP_COIN_FROM", -1);
        int coinUntil = envInt("JDSP_COIN_UNTIL", coinFrom < 0 ? -1 : coinFrom + 8);
        int startFrom = envInt("JDSP_START_FROM", -1);
        int startUntil = envInt("JDSP_START_UNTIL", startFrom < 0 ? -1 : startFrom + 8);
        List<Short> samples = new ArrayList<>();
        for (int frame = 0; frame < frames; frame++) {
            MachineInputs inputs = new MachineInputs();
            if (coinFrom >= 0 && frame >= coinFrom && frame < coinUntil) {
                inputs.coin1 = true;
            }
            if (startFrom >= 0 && frame >= startFrom && frame < startUntil) {
                inputs.player1.start = true;
            }
            machine.setInputs(inputs);
            machine.runFrame();
            samples.clear();
            machine.drainAudio(samples);
        }
        BmpWriter.write(Path.of(options.screenshot), machine.framebuffer(),
                machine.screenWidth(), machine.screenHeight());
        System.out.println("wrote " + options.screenshot);
        return 0;
    }

    private int runWindow(Machine machine) throws Exception {
        int scale = Math.max(1, options.scale);
        int displayW = machine.displayWidth();
        int displayH = machine.displayHeight();
        BufferedImage image = new BufferedImage(machine.screenWidth(), machine.screenHeight(),
                BufferedImage.TYPE_INT_ARGB);
        ScreenPanel panel = new ScreenPanel(image, displayW * scale, displayH * scale);

        JFrame frame = new JFrame("jDSP - " + machine.title());
        frame.setDefaultCloseOperation(WindowConstants.EXIT_ON_CLOSE);
        frame.setContentPane(panel);
        frame.pack();
        frame.setResizable(true);
        frame.setLocationRelativeTo(null);
        if (options.fullscreen) {
            frame.setExtendedState(JFrame.MAXIMIZED_BOTH);
            frame.setUndecorated(true);
        }
        KeyAdapter keys = new KeyAdapter() {
            @Override
            public void keyPressed(KeyEvent e) {
                onKeyPressed(e.getKeyCode(), machine, frame);
            }

            @Override
            public void keyReleased(KeyEvent e) {
                keysDown.remove(e.getKeyCode());
            }
        };
        // Clicking the framebuffer focuses the panel, not the frame. Listen on
        // both, and also dispatch globally so keys work whenever this window
        // is active.
        frame.addKeyListener(keys);
        panel.addKeyListener(keys);
        KeyboardFocusManager.getCurrentKeyboardFocusManager().addKeyEventDispatcher(event -> {
            if (!running || !frame.isDisplayable() || !(event instanceof KeyEvent)) {
                return false;
            }
            KeyEvent keyEvent = (KeyEvent) event;
            java.awt.Component source = keyEvent.getComponent();
            if (source != null && source != frame
                    && SwingUtilities.getWindowAncestor(source) != frame) {
                return false;
            }
            if (source == null && !frame.isActive()) {
                return false;
            }
            // Consume here so the panel/frame listeners do not toggle pause twice.
            if (keyEvent.getID() == KeyEvent.KEY_PRESSED) {
                onKeyPressed(keyEvent.getKeyCode(), machine, frame);
            } else if (keyEvent.getID() == KeyEvent.KEY_RELEASED) {
                keysDown.remove(keyEvent.getKeyCode());
            }
            return true;
        });
        frame.setFocusable(true);
        frame.setAutoRequestFocus(true);
        frame.setVisible(true);
        SwingUtilities.invokeLater(panel::requestFocusInWindow);

        SourceDataLine line = null;
        if (!options.mute) {
            AudioFormat format = new AudioFormat(machine.sampleRate(), 16, 1, true, false);
            DataLine.Info info = new DataLine.Info(SourceDataLine.class, format);
            if (AudioSystem.isLineSupported(info)) {
                line = (SourceDataLine) AudioSystem.getLine(info);
                line.open(format, machine.sampleRate());
                line.start();
            }
        }
        SourceDataLine audioLine = line;

        int delayMs = Math.max(1, (int) Math.round(1000.0 / machine.framesPerSecond()));
        Timer timer = new Timer(delayMs, e -> {
            if (!running) {
                ((Timer) e.getSource()).stop();
                if (audioLine != null) {
                    audioLine.close();
                }
                return;
            }
            if (paused) {
                return;
            }
            MachineInputs inputs = collectInputs();
            machine.setInputs(inputs);
            machine.runFrame();

            int[] src = machine.framebuffer();
            int[] dst = ((DataBufferInt) image.getRaster().getDataBuffer()).getData();
            System.arraycopy(src, 0, dst, 0, Math.min(src.length, dst.length));
            panel.repaint();

            if (audioLine != null) {
                List<Short> samples = new ArrayList<>();
                machine.drainAudio(samples);
                if (!samples.isEmpty()) {
                    ByteBuffer buffer = ByteBuffer.allocate(samples.size() * 2).order(ByteOrder.LITTLE_ENDIAN);
                    for (short sample : samples) {
                        buffer.putShort(sample);
                    }
                    byte[] bytes = buffer.array();
                    audioLine.write(bytes, 0, bytes.length);
                }
            }
        });
        timer.start();

        while (running && frame.isDisplayable()) {
            Thread.sleep(50);
        }
        timer.stop();
        if (audioLine != null) {
            audioLine.close();
        }
        return 0;
    }

    private static int envInt(String name, int fallback) {
        String value = System.getenv(name);
        if (value == null || value.isBlank()) {
            return fallback;
        }
        return Integer.parseInt(value);
    }

    private void onKeyPressed(int keyCode, Machine machine, JFrame frame) {
        keysDown.add(keyCode);
        if (keyCode == KeyEvent.VK_ESCAPE) {
            running = false;
            frame.dispose();
        } else if (keyCode == KeyEvent.VK_F3) {
            machine.reset();
        } else if (keyCode == KeyEvent.VK_P || keyCode == KeyEvent.VK_F2) {
            paused = !paused;
            updateTitle(frame, machine);
        }
    }

    private void updateTitle(JFrame frame, Machine machine) {
        String title = "jDSP - " + machine.title();
        if (paused) {
            title += " [PAUSED]";
        }
        frame.setTitle(title);
    }

    private MachineInputs collectInputs() {
        MachineInputs inputs = new MachineInputs();
        inputs.player1.up = down(KeyEvent.VK_UP);
        inputs.player1.down = down(KeyEvent.VK_DOWN);
        inputs.player1.left = down(KeyEvent.VK_LEFT);
        inputs.player1.right = down(KeyEvent.VK_RIGHT);
        inputs.player1.button1 = down(KeyEvent.VK_CONTROL) || down(KeyEvent.VK_SPACE);
        inputs.player1.button2 = down(KeyEvent.VK_ALT) || down(KeyEvent.VK_Z);
        inputs.player1.button3 = down(KeyEvent.VK_X);
        inputs.player1.button4 = down(KeyEvent.VK_C);
        inputs.player1.start = down(KeyEvent.VK_1);
        inputs.player1.select = down(KeyEvent.VK_3);

        inputs.player2.up = down(KeyEvent.VK_R);
        inputs.player2.down = down(KeyEvent.VK_F);
        inputs.player2.left = down(KeyEvent.VK_D);
        inputs.player2.right = down(KeyEvent.VK_G);
        inputs.player2.button1 = down(KeyEvent.VK_A);
        inputs.player2.button2 = down(KeyEvent.VK_S);
        inputs.player2.button3 = down(KeyEvent.VK_Q);
        inputs.player2.button4 = down(KeyEvent.VK_W);
        inputs.player2.start = down(KeyEvent.VK_2);
        inputs.player2.select = down(KeyEvent.VK_4);

        inputs.coin1 = down(KeyEvent.VK_5);
        inputs.coin2 = down(KeyEvent.VK_6);
        inputs.service = down(KeyEvent.VK_F1);

        for (Key key : Key.values()) {
            Integer code = keyCode(key);
            if (code != null) {
                inputs.keys[key.ordinal()] = down(code);
            }
        }
        return inputs;
    }

    private boolean down(int keyCode) {
        return keysDown.contains(keyCode);
    }

    private static Integer keyCode(Key key) {
        return switch (key) {
            case A -> KeyEvent.VK_A;
            case B -> KeyEvent.VK_B;
            case C -> KeyEvent.VK_C;
            case D -> KeyEvent.VK_D;
            case E -> KeyEvent.VK_E;
            case F -> KeyEvent.VK_F;
            case G -> KeyEvent.VK_G;
            case H -> KeyEvent.VK_H;
            case I -> KeyEvent.VK_I;
            case J -> KeyEvent.VK_J;
            case K -> KeyEvent.VK_K;
            case L -> KeyEvent.VK_L;
            case M -> KeyEvent.VK_M;
            case N -> KeyEvent.VK_N;
            case O -> KeyEvent.VK_O;
            case P -> KeyEvent.VK_P;
            case Q -> KeyEvent.VK_Q;
            case R -> KeyEvent.VK_R;
            case S -> KeyEvent.VK_S;
            case T -> KeyEvent.VK_T;
            case U -> KeyEvent.VK_U;
            case V -> KeyEvent.VK_V;
            case W -> KeyEvent.VK_W;
            case X -> KeyEvent.VK_X;
            case Y -> KeyEvent.VK_Y;
            case Z -> KeyEvent.VK_Z;
            case NUM0 -> KeyEvent.VK_0;
            case NUM1 -> KeyEvent.VK_1;
            case NUM2 -> KeyEvent.VK_2;
            case NUM3 -> KeyEvent.VK_3;
            case NUM4 -> KeyEvent.VK_4;
            case NUM5 -> KeyEvent.VK_5;
            case NUM6 -> KeyEvent.VK_6;
            case NUM7 -> KeyEvent.VK_7;
            case NUM8 -> KeyEvent.VK_8;
            case NUM9 -> KeyEvent.VK_9;
            case ENTER -> KeyEvent.VK_ENTER;
            case SPACE -> KeyEvent.VK_SPACE;
            case LEFT_SHIFT -> KeyEvent.VK_SHIFT;
            case RIGHT_SHIFT -> KeyEvent.VK_SHIFT;
            case LEFT_CTRL -> KeyEvent.VK_CONTROL;
            case RIGHT_CTRL -> KeyEvent.VK_CONTROL;
            case BACKSPACE -> KeyEvent.VK_BACK_SPACE;
            case UP -> KeyEvent.VK_UP;
            case DOWN -> KeyEvent.VK_DOWN;
            case LEFT -> KeyEvent.VK_LEFT;
            case RIGHT -> KeyEvent.VK_RIGHT;
            case COMMA -> KeyEvent.VK_COMMA;
            case PERIOD -> KeyEvent.VK_PERIOD;
            case SEMICOLON -> KeyEvent.VK_SEMICOLON;
            case QUOTE -> KeyEvent.VK_QUOTE;
            case SLASH -> KeyEvent.VK_SLASH;
            case MINUS -> KeyEvent.VK_MINUS;
            case ESCAPE -> KeyEvent.VK_ESCAPE;
            case TAB -> KeyEvent.VK_TAB;
            case CAPS_LOCK -> KeyEvent.VK_CAPS_LOCK;
            case HOME -> KeyEvent.VK_HOME;
            case CBM -> KeyEvent.VK_ALT;
            case EQUALS -> KeyEvent.VK_EQUALS;
            case PLUS -> KeyEvent.VK_ADD;
            case ASTERISK -> KeyEvent.VK_CLOSE_BRACKET;
            case AT -> KeyEvent.VK_OPEN_BRACKET;
            case F1 -> KeyEvent.VK_F1;
            case F2 -> KeyEvent.VK_F2;
            case F3 -> KeyEvent.VK_F3;
            case F4 -> KeyEvent.VK_F4;
            case F5 -> KeyEvent.VK_F5;
            case F6 -> KeyEvent.VK_F6;
            case F7 -> KeyEvent.VK_F7;
            case F8 -> KeyEvent.VK_F8;
            case F9 -> KeyEvent.VK_F9;
            case F10 -> KeyEvent.VK_F10;
            case F11 -> KeyEvent.VK_F11;
            case F12 -> KeyEvent.VK_F12;
            case BACKSLASH -> KeyEvent.VK_BACK_SLASH;
            case BACKQUOTE -> KeyEvent.VK_BACK_QUOTE;
            case DELETE -> KeyEvent.VK_DELETE;
            case LEFT_GUI -> KeyEvent.VK_WINDOWS;
            case RIGHT_GUI -> KeyEvent.VK_WINDOWS;
            case RIGHT_ALT -> KeyEvent.VK_ALT;
        };
    }

    private static final class ScreenPanel extends JPanel {
        private final BufferedImage image;

        ScreenPanel(BufferedImage image, int width, int height) {
            this.image = image;
            setPreferredSize(new Dimension(width, height));
            setBackground(Color.BLACK);
            setFocusable(true);
            addMouseListener(new java.awt.event.MouseAdapter() {
                @Override
                public void mousePressed(java.awt.event.MouseEvent e) {
                    requestFocusInWindow();
                }
            });
        }

        @Override
        protected void paintComponent(Graphics g) {
            super.paintComponent(g);
            Graphics2D g2 = (Graphics2D) g;
            g2.setRenderingHint(RenderingHints.KEY_INTERPOLATION,
                    RenderingHints.VALUE_INTERPOLATION_NEAREST_NEIGHBOR);
            g2.drawImage(image, 0, 0, getWidth(), getHeight(), null);
        }
    }
}
