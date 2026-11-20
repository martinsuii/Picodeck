package com.arcademultitool.companion;

import java.io.BufferedInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;

/** TCP connection to the ESP32 link server (WiFi mode). */
final class TcpLink implements Link {
    private final Socket socket;
    private final OutputStream out;
    private final LineSink sink;
    private volatile boolean running = true;

    TcpLink(String host, int port, LineSink sink) throws IOException {
        this.sink = sink;
        Socket s = new Socket();
        s.connect(new InetSocketAddress(host, port), 8000);
        s.setTcpNoDelay(true);
        s.setKeepAlive(true);
        this.socket = s;
        this.out = s.getOutputStream();

        Thread reader = new Thread(this::readLoop, "pico-tcp-read");
        reader.setDaemon(true);
        reader.start();
    }

    private void readLoop() {
        byte[] buf = new byte[4096];
        StringBuilder line = new StringBuilder();
        try (InputStream in = new BufferedInputStream(socket.getInputStream(), 8192)) {
            int n;
            while (running && (n = in.read(buf)) >= 0) {
                String text = new String(buf, 0, n, StandardCharsets.UTF_8);
                for (int i = 0; i < text.length(); i++) {
                    char ch = text.charAt(i);
                    if (ch == '\n') {
                        String s = line.toString();
                        if (s.endsWith("\r")) s = s.substring(0, s.length() - 1);
                        line.setLength(0);
                        sink.onLine(s);
                    } else if (ch != '\r') {
                        line.append(ch);
                    }
                }
                if (line.length() > 8192) {
                    String s = line.substring(line.length() - 2048);
                    line.setLength(0);
                    sink.onLine(s);
                }
            }
        } catch (IOException e) {
            if (running) closed("WiFi link failed: " + e.getMessage());
            return;
        }
        if (running) closed("WiFi link closed");
    }

    private void closed(String why) {
        running = false;
        sink.onClosed(why);
    }

    @Override
    public String transport() {
        return "wifi";
    }

    @Override
    public void send(String line) throws Exception {
        synchronized (this) {
            out.write(line.getBytes(StandardCharsets.UTF_8));
            out.flush();
        }
    }

    @Override
    public void close() {
        running = false;
        try {
            socket.close();
        } catch (IOException ignored) {
            /* ignore */
        }
    }
}
