package com.arcademultitool.companion;

import android.hardware.usb.UsbConstants;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbDeviceConnection;
import android.hardware.usb.UsbEndpoint;
import android.hardware.usb.UsbInterface;
import android.hardware.usb.UsbManager;

import java.nio.charset.StandardCharsets;

/** Minimal CDC-ACM serial port (the PicoPad's USB console). */
final class UsbCdc implements Link {
    private final UsbDeviceConnection conn;
    private final UsbInterface dataIface;
    private final UsbEndpoint in;
    private final UsbEndpoint out;
    private final LineSink sink;
    private volatile boolean running = true;

    UsbCdc(UsbManager manager, UsbDevice device, int baud, LineSink sink) throws Exception {
        this.sink = sink;

        UsbInterface comm = null;
        UsbInterface data = null;
        UsbEndpoint epIn = null;
        UsbEndpoint epOut = null;

        for (int i = 0; i < device.getInterfaceCount(); i++) {
            UsbInterface itf = device.getInterface(i);
            if (itf.getInterfaceClass() == UsbConstants.USB_CLASS_COMM && comm == null) comm = itf;
            for (int j = 0; j < itf.getEndpointCount(); j++) {
                UsbEndpoint ep = itf.getEndpoint(j);
                if (ep.getType() != UsbConstants.USB_ENDPOINT_XFER_BULK) continue;
                if (ep.getDirection() == UsbConstants.USB_DIR_IN && epIn == null) {
                    epIn = ep;
                    data = itf;
                } else if (ep.getDirection() == UsbConstants.USB_DIR_OUT && epOut == null) {
                    epOut = ep;
                    data = itf;
                }
            }
        }
        if (epIn == null || epOut == null) throw new Exception("No USB serial endpoints found");

        UsbDeviceConnection c = manager.openDevice(device);
        if (c == null) throw new Exception("USB permission denied (allow it, then connect again)");

        if (comm != null && comm != data && !c.claimInterface(comm, true)) {
            c.close();
            throw new Exception("Cannot claim USB control interface");
        }
        if (!c.claimInterface(data, true)) {
            if (comm != null) c.releaseInterface(comm);
            c.close();
            throw new Exception("Cannot claim USB data interface");
        }

        this.conn = c;
        this.dataIface = data;
        this.in = epIn;
        this.out = epOut;

        // 8N1, no flow control.
        int idx = comm != null ? comm.getId() : data.getId();
        byte[] coding = new byte[7];
        coding[0] = (byte) (baud & 0xff);
        coding[1] = (byte) ((baud >> 8) & 0xff);
        coding[2] = (byte) ((baud >> 16) & 0xff);
        coding[3] = (byte) ((baud >> 24) & 0xff);
        coding[4] = 0; // 1 stop bit
        coding[5] = 0; // no parity
        coding[6] = 8; // data bits
        try {
            c.controlTransfer(0x21, 0x20, 0, idx, coding, coding.length, 1000);
            c.controlTransfer(0x21, 0x22, 0x03, idx, null, 0, 1000); // DTR | RTS
        } catch (Exception ignored) {
            /* some implementations do not need this */
        }

        Thread reader = new Thread(this::readLoop, "pico-usb-read");
        reader.setDaemon(true);
        reader.start();
    }

    private void readLoop() {
        byte[] buf = new byte[4096];
        StringBuilder line = new StringBuilder();
        while (running) {
            int n;
            try {
                n = conn.bulkTransfer(in, buf, buf.length, 250);
            } catch (Exception e) {
                if (running) closed("USB read failed: " + e.getMessage());
                return;
            }
            if (n < 0) continue;
            if (n == 0) continue;
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
    }

    private void closed(String why) {
        running = false;
        sink.onClosed(why);
    }

    @Override
    public String transport() {
        return "usb";
    }

    @Override
    public void send(String line) throws Exception {
        byte[] data = line.getBytes(StandardCharsets.UTF_8);
        int max = Math.max(1, out.getMaxPacketSize());
        int off = 0;
        while (off < data.length) {
            int len = Math.min(max, data.length - off);
            byte[] chunk = new byte[len];
            System.arraycopy(data, off, chunk, 0, len);
            int wrote = conn.bulkTransfer(out, chunk, len, 1000);
            if (wrote < 0) throw new Exception("USB write failed");
            off += len;
        }
    }

    @Override
    public void close() {
        if (!running) return;
        running = false;
        try {
            conn.releaseInterface(dataIface);
        } catch (Exception ignored) {
            /* ignore */
        }
        try {
            conn.close();
        } catch (Exception ignored) {
            /* ignore */
        }
    }
}
