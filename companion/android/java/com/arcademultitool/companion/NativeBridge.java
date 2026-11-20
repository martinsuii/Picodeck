package com.arcademultitool.companion;

import android.app.Activity;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.hardware.usb.UsbConstants;
import android.hardware.usb.UsbDevice;
import android.hardware.usb.UsbInterface;
import android.hardware.usb.UsbManager;
import android.net.Uri;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.provider.DocumentsContract;
import android.util.Base64;
import android.webkit.JavascriptInterface;
import android.webkit.WebView;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
import java.util.Map;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Java side of the Android app. The web UI calls this through
 * window.PicoAndroid; local-api.js turns those calls into the same /api routes
 * the desktop companion server exposes.
 */
public class NativeBridge {
    private static final String ACTION_USB =
            "com.arcademultitool.companion.USB_PERMISSION";
    private static final int REQ_TREE = 9201;
    private static final int REQ_SAVE = 9202;
    private static final int REQ_BACKUP = 9203;

    private final Activity activity;
    private final WebView web;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final ExecutorService pool = Executors.newCachedThreadPool();
    private final UsbManager usb;
    private final ArrayDeque<String> history = new ArrayDeque<>();

    private volatile Link link;
    private volatile String transport;
    private volatile JSONObject info;
    private volatile boolean connected;
    private volatile Object activeToken;

    private String pendingTree;
    private String pendingSave;
    private byte[] pendingSaveBytes;
    private String pendingSaveName;

    private String pendingBackup;
    private File pendingBackupFile;

    private String pendingUsb;
    private int pendingUsbId;
    private int pendingUsbBaud;

    private final BroadcastReceiver receiver =
            new BroadcastReceiver() {
                @Override
                public void onReceive(Context context, Intent intent) {
                    if (!ACTION_USB.equals(intent.getAction())) return;
                    boolean granted =
                            intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false);
                    final String id = pendingUsb;
                    final int devId = pendingUsbId;
                    final int baud = pendingUsbBaud;
                    pendingUsb = null;
                    if (id == null) return;
                    if (!granted) {
                        replyError(id, "USB access was not allowed");
                        return;
                    }
                    pool.execute(
                            () -> {
                                try {
                                    UsbDevice dev = findUsbDevice(devId);
                                    if (dev == null) throw new Exception("USB device is gone");
                                    openUsb(dev, baud);
                                    replyOk(id, stateObj());
                                } catch (Exception e) {
                                    replyError(id, message(e));
                                }
                            });
                }
            };

    NativeBridge(Activity activity, WebView web) {
        this.activity = activity;
        this.web = web;
        this.usb = (UsbManager) activity.getSystemService(Context.USB_SERVICE);

        IntentFilter filter = new IntentFilter(ACTION_USB);
        if (Build.VERSION.SDK_INT >= 33) {
            activity.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED);
        } else {
            activity.registerReceiver(receiver, filter);
        }
    }

    void shutdown() {
        try {
            activity.unregisterReceiver(receiver);
        } catch (Exception ignored) {
            /* ignore */
        }
        Link l = link;
        link = null;
        activeToken = null;
        connected = false;
        if (l != null) l.close();
        pool.shutdownNow();
    }

    // --- JS entry point -----------------------------------------------------

    @JavascriptInterface
    public void call(String id, String method, String argsJson) {
        JSONObject args;
        try {
            args = argsJson == null || argsJson.isEmpty() ? new JSONObject() : new JSONObject(argsJson);
        } catch (Exception e) {
            replyError(id, "Bad arguments");
            return;
        }

        // These need the UI thread and answer later, from onActivityResult.
        if ("pickTree".equals(method)) {
            pickTree(id);
            return;
        }
        if ("saveFile".equals(method)) {
            saveFile(id, args);
            return;
        }
        if ("backup".equals(method)) {
            backup(id, args);
            return;
        }
        if ("usbConnect".equals(method)) {
            usbConnect(id, args);
            return;
        }

        pool.execute(
                () -> {
                    try {
                        Object data = doCall(method, args);
                        replyOk(id, data);
                    } catch (Exception e) {
                        replyError(id, message(e));
                    }
                });
    }

    private Object doCall(String method, JSONObject a) throws Exception {
        switch (method) {
            case "list":
                return SdStore.list(activity, a.getString("tree"), a.optString("rel", ""));
            case "stat":
                return SdStore.stat(activity, a.getString("tree"), a.optString("rel", ""));
            case "exists":
                return SdStore.exists(activity, a.getString("tree"), a.optString("rel", ""));
            case "readText":
                return SdStore.readText(activity, a.getString("tree"), a.getString("rel"));
            case "readBytes":
                return Base64.encodeToString(
                        SdStore.readBytes(activity, a.getString("tree"), a.getString("rel")),
                        Base64.NO_WRAP);
            case "writeBytes":
                return SdStore.writeBytes(
                        activity,
                        a.getString("tree"),
                        a.getString("rel"),
                        Base64.decode(a.optString("base64", ""), Base64.DEFAULT));
            case "writeText":
                return SdStore.writeBytes(
                        activity,
                        a.getString("tree"),
                        a.getString("rel"),
                        a.optString("text", "").getBytes(StandardCharsets.UTF_8));
            case "mkdir":
                return SdStore.mkdir(activity, a.getString("tree"), a.getString("rel"));
            case "delete":
                return SdStore.delete(activity, a.getString("tree"), a.getString("rel"));
            case "rename":
                return SdStore.rename(
                        activity,
                        a.getString("tree"),
                        a.getString("rel"),
                        a.getString("name"));
            case "writeCache":
                return writeCache(a.optString("name", "restore.zip"), a.optString("base64", ""));
            case "restore":
                return SdStore.restore(activity, a.getString("tree"), a.getString("path"));

            case "assetList":
                return assetList(a.optString("dir", ""));
            case "assetText":
                return new String(readAsset(a.getString("path")), StandardCharsets.UTF_8);

            case "usbList":
                return usbList();
            case "tcpConnect":
                return tcpConnect(a);
            case "disconnect":
                disconnect();
                return stateObj();
            case "send":
                return send(a);
            default:
                throw new Exception("Unknown native call: " + method);
        }
    }

    // --- SD selection / saving ---------------------------------------------

    private void pickTree(String id) {
        main.post(
                () -> {
                    if (pendingTree != null) {
                        replyError(id, "Another folder picker is open");
                        return;
                    }
                    try {
                        Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
                        i.addFlags(
                                Intent.FLAG_GRANT_READ_URI_PERMISSION
                                        | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                                        | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
                                        | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION);
                        pendingTree = id;
                        activity.startActivityForResult(i, REQ_TREE);
                    } catch (Exception e) {
                        pendingTree = null;
                        replyError(id, message(e));
                    }
                });
    }

    private void saveFile(String id, JSONObject a) {
        final String name = a.optString("name", "file.bin");
        final String mime = a.optString("mime", "application/octet-stream");
        final byte[] data;
        try {
            data = Base64.decode(a.optString("base64", ""), Base64.DEFAULT);
        } catch (Exception e) {
            replyError(id, "Bad file data");
            return;
        }
        main.post(
                () -> {
                    if (pendingSave != null) {
                        replyError(id, "Another save dialog is open");
                        return;
                    }
                    try {
                        Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT);
                        i.addCategory(Intent.CATEGORY_OPENABLE);
                        i.setType(mime);
                        i.putExtra(Intent.EXTRA_TITLE, name);
                        pendingSave = id;
                        pendingSaveName = name;
                        pendingSaveBytes = data;
                        activity.startActivityForResult(i, REQ_SAVE);
                    } catch (Exception e) {
                        pendingSave = null;
                        replyError(id, message(e));
                    }
                });
    }

    private void backup(String id, JSONObject a) {
        final String tree = a.optString("tree", "");
        if (tree.isEmpty()) {
            replyError(id, "Choose the SD card folder first");
            return;
        }
        pool.execute(
                () -> {
                    try {
                        File dir = new File(activity.getCacheDir(), "backups");
                        final File zip = SdStore.backup(activity, tree, dir);
                        main.post(
                                () -> {
                                    if (pendingBackup != null) {
                                        replyError(id, "Another save dialog is open");
                                        return;
                                    }
                                    try {
                                        Intent i = new Intent(Intent.ACTION_CREATE_DOCUMENT);
                                        i.addCategory(Intent.CATEGORY_OPENABLE);
                                        i.setType("application/zip");
                                        i.putExtra(Intent.EXTRA_TITLE, zip.getName());
                                        pendingBackup = id;
                                        pendingBackupFile = zip;
                                        activity.startActivityForResult(i, REQ_BACKUP);
                                    } catch (Exception e) {
                                        pendingBackup = null;
                                        pendingBackupFile = null;
                                        replyError(id, message(e));
                                    }
                                });
                    } catch (Exception e) {
                        replyError(id, message(e));
                    }
                });
    }

    @SuppressWarnings("deprecation")
    boolean onActivityResult(int request, int result, Intent data) {
        if (request == REQ_TREE) {
            final String id = pendingTree;
            pendingTree = null;
            if (id == null) return true;
            if (result != Activity.RESULT_OK || data == null || data.getData() == null) {
                replyError(id, "No folder chosen");
                return true;
            }
            Uri uri = data.getData();
            try {
                activity.getContentResolver()
                        .takePersistableUriPermission(
                                uri,
                                Intent.FLAG_GRANT_READ_URI_PERMISSION
                                        | Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
            } catch (Exception ignored) {
                /* some providers do not offer persistable grants */
            }
            try {
                JSONObject o = new JSONObject();
                o.put("uri", uri.toString());
                o.put("label", treeLabel(uri));
                replyOk(id, o);
            } catch (Exception e) {
                replyError(id, message(e));
            }
            return true;
        }

        if (request == REQ_SAVE) {
            final String id = pendingSave;
            final byte[] bytes = pendingSaveBytes;
            final String name = pendingSaveName;
            pendingSave = null;
            pendingSaveBytes = null;
            pendingSaveName = null;
            if (id == null) return true;
            if (result != Activity.RESULT_OK || data == null || data.getData() == null) {
                replyError(id, "Save cancelled");
                return true;
            }
            final Uri target = data.getData();
            pool.execute(
                    () -> {
                        try {
                            try (OutputStream out =
                                    activity.getContentResolver().openOutputStream(target)) {
                                if (out == null) throw new Exception("Cannot open the chosen file");
                                out.write(bytes == null ? new byte[0] : bytes);
                                out.flush();
                            }
                            JSONObject o = new JSONObject();
                            o.put("name", name);
                            o.put("size", bytes == null ? 0 : bytes.length);
                            replyOk(id, o);
                        } catch (Exception e) {
                            replyError(id, message(e));
                        }
                    });
            return true;
        }

        if (request == REQ_BACKUP) {
            final String id = pendingBackup;
            final File zip = pendingBackupFile;
            pendingBackup = null;
            pendingBackupFile = null;
            if (id == null) return true;
            if (result != Activity.RESULT_OK || data == null || data.getData() == null) {
                if (zip != null) //noinspection ResultOfMethodCallIgnored
                    zip.delete();
                replyError(id, "Backup cancelled");
                return true;
            }
            final Uri target = data.getData();
            pool.execute(
                    () -> {
                        try {
                            try (InputStream in = new java.io.FileInputStream(zip);
                                    OutputStream out =
                                            activity.getContentResolver().openOutputStream(target)) {
                                if (out == null) throw new Exception("Cannot open the chosen file");
                                byte[] buf = new byte[65536];
                                int n;
                                while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
                                out.flush();
                            }
                            JSONObject o = new JSONObject();
                            o.put("name", zip.getName());
                            o.put("size", zip.length());
                            //noinspection ResultOfMethodCallIgnored
                            zip.delete();
                            replyOk(id, o);
                        } catch (Exception e) {
                            replyError(id, message(e));
                        }
                    });
            return true;
        }

        return false;
    }

    private String treeLabel(Uri uri) {
        try {
            String id = DocumentsContract.getTreeDocumentId(uri);
            Uri doc = DocumentsContract.buildDocumentUriUsingTree(uri, id);
            JSONObject meta = SdStore.statUri(activity, doc);
            String name = meta.optString("name", "");
            if (!name.isEmpty()) return name;
            int colon = id.indexOf(':');
            return colon >= 0 && colon + 1 < id.length() ? id.substring(colon + 1) : id;
        } catch (Exception e) {
            return "SD card";
        }
    }

    private JSONObject writeCache(String name, String base64) throws Exception {
        String clean = new File(name == null ? "restore.zip" : name).getName();
        if (clean.isEmpty() || clean.equals(".") || clean.equals("..")) clean = "restore.zip";
        File dir = new File(activity.getCacheDir(), "restore");
        if (!dir.exists() && !dir.mkdirs()) throw new Exception("Cannot create cache folder");
        File f = new File(dir, clean);
        byte[] data = Base64.decode(base64, Base64.DEFAULT);
        try (FileOutputStream out = new FileOutputStream(f)) {
            out.write(data);
        }
        JSONObject o = new JSONObject();
        o.put("path", f.getAbsolutePath());
        o.put("size", f.length());
        return o;
    }

    // --- assets -------------------------------------------------------------

    private byte[] readAsset(String path) throws Exception {
        try (InputStream in = activity.getAssets().open(path)) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buf = new byte[65536];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            return out.toByteArray();
        }
    }

    private JSONArray assetList(String dir) throws Exception {
        JSONArray out = new JSONArray();
        String[] names = activity.getAssets().list(dir);
        if (names == null) return out;
        for (String name : names) {
            String path = dir.isEmpty() ? name : dir + "/" + name;
            int size = 0;
            try (InputStream in = activity.getAssets().open(path)) {
                size = in.available();
            } catch (Exception ignored) {
                /* directories cannot be opened */
            }
            JSONObject o = new JSONObject();
            o.put("name", name);
            o.put("size", size);
            o.put("mtime", 0);
            out.put(o);
        }
        return out;
    }

    // --- device -------------------------------------------------------------

    private JSONArray usbList() {
        JSONArray out = new JSONArray();
        if (usb == null) return out;
        for (Map.Entry<String, UsbDevice> e : usb.getDeviceList().entrySet()) {
            UsbDevice d = e.getValue();
            if (!hasCdc(d)) continue;
            try {
                JSONObject o = new JSONObject();
                o.put("id", d.getDeviceId());
                o.put("label", usbLabel(d));
                out.put(o);
            } catch (Exception ignored) {
                /* skip */
            }
        }
        return out;
    }

    private static boolean hasCdc(UsbDevice d) {
        for (int i = 0; i < d.getInterfaceCount(); i++) {
            if (d.getInterface(i).getInterfaceClass() == UsbConstants.USB_CLASS_COMM) return true;
        }
        return false;
    }

    private static String usbLabel(UsbDevice d) {
        StringBuilder s = new StringBuilder();
        String name = d.getProductName();
        String maker = d.getManufacturerName();
        if (name != null && !name.isEmpty()) s.append(name);
        if (maker != null && !maker.isEmpty()) {
            if (s.length() > 0) s.append(" · ");
            s.append(maker);
        }
        if (s.length() == 0) {
            s.append(String.format("USB %04x:%04x", d.getVendorId(), d.getProductId()));
        }
        return s.toString();
    }

    private UsbDevice findUsbDevice(int id) {
        if (usb == null) return null;
        for (UsbDevice d : usb.getDeviceList().values()) {
            if (d.getDeviceId() == id) return d;
        }
        return null;
    }

    private void usbConnect(String id, JSONObject a) {
        final int devId = a.optInt("id", 0);
        final int baud = a.optInt("baud", 115200);
        final UsbDevice dev = findUsbDevice(devId);
        if (usb == null || dev == null) {
            replyError(id, "USB device not found");
            return;
        }
        if (usb.hasPermission(dev)) {
            pool.execute(
                    () -> {
                        try {
                            openUsb(dev, baud);
                            replyOk(id, stateObj());
                        } catch (Exception e) {
                            replyError(id, message(e));
                        }
                    });
            return;
        }

        pendingUsb = id;
        pendingUsbId = devId;
        pendingUsbBaud = baud;

        int flags = PendingIntent.FLAG_UPDATE_CURRENT;
        if (Build.VERSION.SDK_INT >= 31) flags |= PendingIntent.FLAG_MUTABLE;
        PendingIntent pi =
                PendingIntent.getBroadcast(
                        activity,
                        0,
                        new Intent(ACTION_USB).setPackage(activity.getPackageName()),
                        flags);
        usb.requestPermission(dev, pi);

        main.postDelayed(
                () -> {
                    if (id.equals(pendingUsb)) {
                        pendingUsb = null;
                        replyError(id, "USB permission request timed out");
                    }
                },
                60000);
    }

    private void openUsb(UsbDevice dev, int baud) throws Exception {
        Object token = new Object();
        UsbCdc l = new UsbCdc(usb, dev, baud, sink(token));
        JSONObject inf = new JSONObject();
        inf.put("path", "usb:" + dev.getDeviceId());
        inf.put("label", usbLabel(dev));
        inf.put("baud", baud);
        attach(l, "usb", inf, "USB " + usbLabel(dev), token);
    }

    private JSONObject tcpConnect(JSONObject a) throws Exception {
        String host = a.optString("host", "").trim();
        int port = a.optInt("port", 2323);
        if (host.isEmpty()) throw new Exception("No host given");
        Object token = new Object();
        TcpLink l = new TcpLink(host, port, sink(token));
        JSONObject inf = new JSONObject();
        inf.put("host", host);
        inf.put("port", port);
        attach(l, "wifi", inf, "WiFi " + host + ":" + port, token);
        return stateObj();
    }

    private void attach(Link l, String tr, JSONObject inf, String what, Object token) {
        Link old = link;
        activeToken = token;
        if (old != null) old.close();
        link = l;
        transport = tr;
        info = inf;
        connected = true;
        synchronized (history) {
            history.clear();
        }
        record("# connected to " + what);
        emit("state", stateObj());
    }

    private void disconnect() {
        Link l = link;
        link = null;
        activeToken = null;
        boolean was = connected;
        connected = false;
        transport = null;
        info = null;
        if (l != null) l.close();
        if (was) {
            record("# disconnected");
            emit("state", stateObj());
        }
    }

    private JSONObject send(JSONObject a) throws Exception {
        String line = a.optString("line", "").replaceAll("[\\r\\n]+", " ").trim();
        Link l = link;
        if (l == null || !connected) throw new Exception("Not connected");
        if (line.isEmpty()) {
            JSONObject o = new JSONObject();
            o.put("sent", "");
            return o;
        }
        String target = a.optString("target", "pico");
        if ("wifi".equals(transport) && "esp".equals(target) && !line.startsWith("!")) {
            line = "!" + line;
        }
        record("> " + line);
        l.send(line + "\n");
        JSONObject o = new JSONObject();
        o.put("sent", line);
        return o;
    }

    private LineSink sink(final Object token) {
        return new LineSink() {
            @Override
            public void onLine(String line) {
                if (token != activeToken) return;
                record(line);
            }

            @Override
            public void onClosed(String why) {
                if (token != activeToken) return;
                if (!connected) return;
                activeToken = null;
                link = null;
                connected = false;
                transport = null;
                info = null;
                if (why != null && !why.isEmpty()) record("# " + why);
                emit("state", stateObj());
            }
        };
    }

    private JSONObject stateObj() {
        JSONObject o = new JSONObject();
        try {
            o.put("connected", connected);
            o.put("transport", transport == null ? JSONObject.NULL : transport);
            o.put("info", info == null ? JSONObject.NULL : info);
            o.put("serialAvailable", true);
            o.put("serialError", JSONObject.NULL);
            o.put("history", historyJson());
        } catch (Exception ignored) {
            /* cannot happen */
        }
        return o;
    }

    private JSONArray historyJson() {
        JSONArray a = new JSONArray();
        synchronized (history) {
            for (String line : history) a.put(line);
        }
        return a;
    }

    private void record(String line) {
        synchronized (history) {
            history.addLast(line);
            while (history.size() > 400) history.removeFirst();
        }
        JSONObject o = new JSONObject();
        try {
            o.put("line", line);
        } catch (Exception ignored) {
            /* ignore */
        }
        emit("line", o);
    }

    // --- JS replies ---------------------------------------------------------

    private void replyOk(String id, Object data) {
        try {
            JSONObject o = new JSONObject();
            o.put("ok", true);
            o.put("data", data == null ? JSONObject.NULL : data);
            postJs("window.__picoResult(" + JSONObject.quote(id) + "," + JSONObject.quote(o.toString()) + ")");
        } catch (Exception e) {
            replyError(id, message(e));
        }
    }

    private void replyError(String id, String error) {
        try {
            JSONObject o = new JSONObject();
            o.put("ok", false);
            o.put("error", error == null ? "Native call failed" : error);
            postJs("window.__picoResult(" + JSONObject.quote(id) + "," + JSONObject.quote(o.toString()) + ")");
        } catch (Exception ignored) {
            /* ignore */
        }
    }

    private void emit(String name, JSONObject data) {
        try {
            postJs(
                    "window.__picoEvent("
                            + JSONObject.quote(name)
                            + ","
                            + JSONObject.quote(data.toString())
                            + ")");
        } catch (Exception ignored) {
            /* ignore */
        }
    }

    private void postJs(String js) {
        main.post(
                () -> {
                    try {
                        web.evaluateJavascript(js, null);
                    } catch (Exception ignored) {
                        /* the WebView may be gone */
                    }
                });
    }

    private static String message(Throwable t) {
        if (t == null) return "Native call failed";
        String m = t.getMessage();
        return m == null || m.isEmpty() ? t.getClass().getSimpleName() : m;
    }
}
