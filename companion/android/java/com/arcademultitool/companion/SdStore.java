package com.arcademultitool.companion;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.BufferedOutputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;

/**
 * SD-card access through a Storage Access Framework tree URI.
 *
 * Everything is done with DocumentsContract so the app has no AndroidX
 * dependency and no storage permission.
 */
final class SdStore {
    private static final String MIME_DIR = DocumentsContract.Document.MIME_TYPE_DIR;
    private static final String MIME_FILE = "application/octet-stream";

    private SdStore() {}

    // --- path helpers -------------------------------------------------------

    private static String[] parts(String rel) {
        String clean = rel == null ? "" : rel.replace('\\', '/');
        while (clean.startsWith("/")) clean = clean.substring(1);
        while (clean.endsWith("/")) clean = clean.substring(0, clean.length() - 1);
        if (clean.isEmpty()) return new String[0];
        String[] p = clean.split("/");
        for (String s : p) {
            if (s.isEmpty() || s.equals(".") || s.equals("..")) {
                throw new IllegalArgumentException("Invalid path");
            }
        }
        return p;
    }

    static String cleanRel(String rel) {
        return String.join("/", parts(rel));
    }

    private static Uri treeRoot(String treeUri) {
        if (treeUri == null || treeUri.isEmpty()) throw new IllegalArgumentException("No SD folder selected");
        Uri uri = Uri.parse(treeUri);
        DocumentsContract.getTreeDocumentId(uri); // throws if this is not a tree URI
        return DocumentsContract.buildDocumentUriUsingTree(
                uri, DocumentsContract.getTreeDocumentId(uri));
    }

    private static Uri findChild(Context ctx, Uri tree, Uri parent, String name) {
        String parentId = DocumentsContract.getDocumentId(parent);
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parentId);
        String[] cols = {
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
        };
        try (Cursor c = ctx.getContentResolver().query(children, cols, null, null, null)) {
            if (c == null) return null;
            while (c.moveToNext()) {
                if (name.equals(c.getString(1))) {
                    return DocumentsContract.buildDocumentUriUsingTree(tree, c.getString(0));
                }
            }
        }
        return null;
    }

    /** Resolve a relative path, or null when it does not exist. */
    static Uri find(Context ctx, String treeUri, String rel) {
        Uri tree = Uri.parse(treeUri);
        Uri cur = treeRoot(treeUri);
        for (String p : parts(rel)) {
            cur = findChild(ctx, tree, cur, p);
            if (cur == null) return null;
        }
        return cur;
    }

    /** Resolve a directory, creating missing components when asked. */
    static Uri dir(Context ctx, String treeUri, String rel, boolean create) {
        Uri tree = Uri.parse(treeUri);
        Uri cur = treeRoot(treeUri);
        for (String p : parts(rel)) {
            Uri next = findChild(ctx, tree, cur, p);
            if (next == null) {
                if (!create) return null;
                try {
                    next = DocumentsContract.createDocument(
                            ctx.getContentResolver(), cur, MIME_DIR, p);
                } catch (Exception e) {
                    throw new IllegalStateException("Cannot create folder " + p + ": " + e.getMessage());
                }
            }
            if (next == null) return null;
            cur = next;
        }
        return cur;
    }

    // --- metadata -----------------------------------------------------------

    private static JSONObject describe(Context ctx, Uri uri, boolean withName) throws Exception {
        String[] cols = {
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE,
            DocumentsContract.Document.COLUMN_SIZE,
            DocumentsContract.Document.COLUMN_LAST_MODIFIED,
        };
        try (Cursor c = ctx.getContentResolver().query(uri, cols, null, null, null)) {
            if (c == null || !c.moveToFirst()) throw new IllegalArgumentException("Not found");
            JSONObject o = new JSONObject();
            if (withName) o.put("name", c.isNull(0) ? "" : c.getString(0));
            String mime = c.isNull(1) ? "" : c.getString(1);
            o.put("dir", MIME_DIR.equals(mime));
            o.put("size", c.isNull(2) ? 0 : c.getLong(2));
            o.put("mtime", c.isNull(3) ? 0 : c.getLong(3));
            return o;
        }
    }

    static JSONArray list(Context ctx, String treeUri, String rel) throws Exception {
        Uri tree = Uri.parse(treeUri);
        Uri d = dir(ctx, treeUri, rel, false);
        JSONArray out = new JSONArray();
        if (d == null) return out;

        String parentId = DocumentsContract.getDocumentId(d);
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, parentId);
        String[] cols = {
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE,
            DocumentsContract.Document.COLUMN_SIZE,
            DocumentsContract.Document.COLUMN_LAST_MODIFIED,
        };
        try (Cursor c = ctx.getContentResolver().query(children, cols, null, null, null)) {
            if (c == null) return out;
            while (c.moveToNext()) {
                JSONObject o = new JSONObject();
                o.put("name", c.isNull(1) ? "" : c.getString(1));
                o.put("dir", MIME_DIR.equals(c.isNull(2) ? "" : c.getString(2)));
                o.put("size", c.isNull(3) ? 0 : c.getLong(3));
                o.put("mtime", c.isNull(4) ? 0 : c.getLong(4));
                out.put(o);
            }
        }
        return out;
    }

    static JSONObject stat(Context ctx, String treeUri, String rel) throws Exception {
        Uri f = find(ctx, treeUri, rel);
        if (f == null) throw new IllegalArgumentException("Not found");
        return describe(ctx, f, true);
    }

    static JSONObject statUri(Context ctx, Uri uri) throws Exception {
        return describe(ctx, uri, true);
    }

    static boolean exists(Context ctx, String treeUri, String rel) {
        try {
            return find(ctx, treeUri, rel) != null;
        } catch (Exception e) {
            return false;
        }
    }

    // --- reading ------------------------------------------------------------

    static String readText(Context ctx, String treeUri, String rel) throws Exception {
        return new String(readBytes(ctx, treeUri, rel), StandardCharsets.UTF_8);
    }

    static byte[] readBytes(Context ctx, String treeUri, String rel) throws Exception {
        Uri f = find(ctx, treeUri, rel);
        if (f == null) throw new IllegalArgumentException("File not found");
        JSONObject meta = describe(ctx, f, false);
        if (meta.optBoolean("dir")) throw new IllegalArgumentException("Is a folder");
        try (InputStream in = ctx.getContentResolver().openInputStream(f)) {
            if (in == null) throw new IllegalArgumentException("Cannot open file");
            long size = meta.optLong("size", 0);
            ByteArrayOutputStream out = new ByteArrayOutputStream(size > 0 ? (int) Math.min(size, 1 << 26) : 65536);
            byte[] buf = new byte[65536];
            int n;
            while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
            return out.toByteArray();
        }
    }

    // --- writing ------------------------------------------------------------

    private static Uri createFile(Context ctx, Uri tree, Uri parent, String name) throws Exception {
        // "*/*" makes providers keep the exact name. "application/octet-stream"
        // is a fallback for providers that reject a wildcard MIME; those may
        // append ".bin", which the rename below removes.
        Uri created = null;
        try {
            created = DocumentsContract.createDocument(
                    ctx.getContentResolver(), parent, "*/*", name);
        } catch (Exception ignored) {
            /* fall back below */
        }
        if (created == null) {
            created = DocumentsContract.createDocument(
                    ctx.getContentResolver(), parent, MIME_FILE, name);
        }
        if (created == null) throw new IllegalArgumentException("Cannot create " + name);

        // Some providers append an extension derived from the MIME type
        // (.bin for octet-stream). Rename to the exact name the launcher needs.
        String actual = describe(ctx, created, true).optString("name", "");
        if (!name.equals(actual)) {
            try {
                Uri renamed = DocumentsContract.renameDocument(ctx.getContentResolver(), created, name);
                if (renamed != null) return renamed;
            } catch (Exception ignored) {
                /* keep the created URI; the caller reports the real name */
            }
        }
        return created;
    }

    private static Uri replaceFile(Context ctx, Uri tree, Uri parent, String name) throws Exception {
        Uri old = findChild(ctx, tree, parent, name);
        if (old != null) {
            try {
                DocumentsContract.deleteDocument(ctx.getContentResolver(), old);
            } catch (Exception e) {
                throw new IllegalStateException("Cannot replace " + name + ": " + e.getMessage());
            }
        }
        return createFile(ctx, tree, parent, name);
    }

    private static void copy(InputStream in, OutputStream out) throws IOException {
        byte[] buf = new byte[65536];
        int n;
        while ((n = in.read(buf)) > 0) out.write(buf, 0, n);
    }

    static JSONObject writeBytes(Context ctx, String treeUri, String rel, byte[] data) throws Exception {
        String clean = cleanRel(rel);
        int slash = clean.lastIndexOf('/');
        String parentRel = slash < 0 ? "" : clean.substring(0, slash);
        String name = slash < 0 ? clean : clean.substring(slash + 1);
        if (name.isEmpty()) throw new IllegalArgumentException("Invalid file name");

        Uri tree = Uri.parse(treeUri);
        Uri parent = dir(ctx, treeUri, parentRel, true);
        if (parent == null) throw new IllegalArgumentException("Cannot create folder");
        Uri file = replaceFile(ctx, tree, parent, name);

        try (OutputStream out = ctx.getContentResolver().openOutputStream(file, "wt")) {
            if (out == null) throw new IllegalArgumentException("Cannot write " + name);
            out.write(data);
            out.flush();
        }

        JSONObject o = new JSONObject();
        o.put("name", name);
        o.put("size", data.length);
        return o;
    }

    static void writeStream(Context ctx, String treeUri, String rel, InputStream in) throws Exception {
        String clean = cleanRel(rel);
        int slash = clean.lastIndexOf('/');
        String parentRel = slash < 0 ? "" : clean.substring(0, slash);
        String name = slash < 0 ? clean : clean.substring(slash + 1);
        if (name.isEmpty()) throw new IllegalArgumentException("Invalid file name");

        Uri tree = Uri.parse(treeUri);
        Uri parent = dir(ctx, treeUri, parentRel, true);
        if (parent == null) throw new IllegalArgumentException("Cannot create folder");
        Uri file = replaceFile(ctx, tree, parent, name);

        try (OutputStream out = new BufferedOutputStream(
                ctx.getContentResolver().openOutputStream(file, "wt"), 65536)) {
            if (out == null) throw new IllegalArgumentException("Cannot write " + name);
            copy(in, out);
            out.flush();
        }
    }

    static JSONObject mkdir(Context ctx, String treeUri, String rel) throws Exception {
        Uri d = dir(ctx, treeUri, rel, true);
        JSONObject o = new JSONObject();
        o.put("created", d != null);
        return o;
    }

    static JSONObject delete(Context ctx, String treeUri, String rel) throws Exception {
        Uri f = find(ctx, treeUri, rel);
        boolean ok = f != null && DocumentsContract.deleteDocument(ctx.getContentResolver(), f);
        JSONObject o = new JSONObject();
        o.put("deleted", ok);
        return o;
    }

    static JSONObject rename(Context ctx, String treeUri, String rel, String name) throws Exception {
        if (name == null || name.isEmpty() || name.contains("/") || name.contains("\\")) {
            throw new IllegalArgumentException("Invalid name");
        }
        Uri f = find(ctx, treeUri, rel);
        if (f == null) throw new IllegalArgumentException("Not found");
        Uri renamed = DocumentsContract.renameDocument(ctx.getContentResolver(), f, name);
        if (renamed == null) throw new IllegalArgumentException("Rename failed");
        JSONObject o = new JSONObject();
        o.put("name", name);
        return o;
    }

    // --- backup / restore ---------------------------------------------------

    static File backup(Context ctx, String treeUri, File dir) throws Exception {
        if (!dir.exists() && !dir.mkdirs()) throw new IOException("Cannot create cache folder");
        File out = new File(dir, "picopad-sd-" + System.currentTimeMillis() + ".zip");
        Uri tree = Uri.parse(treeUri);
        Uri root = treeRoot(treeUri);

        try (ZipOutputStream zip = new ZipOutputStream(new BufferedOutputStream(new FileOutputStream(out), 65536))) {
            walk(ctx, tree, root, "", zip);
        }
        return out;
    }

    private static void walk(Context ctx, Uri tree, Uri dirUri, String prefix, ZipOutputStream zip) throws Exception {
        String dirId = DocumentsContract.getDocumentId(dirUri);
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, dirId);
        String[] cols = {
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE,
        };
        try (Cursor c = ctx.getContentResolver().query(children, cols, null, null, null)) {
            if (c == null) return;
            while (c.moveToNext()) {
                String name = c.isNull(1) ? "" : c.getString(1);
                if (name.isEmpty() || name.equals(".") || name.equals("..")) continue;
                String mime = c.isNull(2) ? "" : c.getString(2);
                Uri child = DocumentsContract.buildDocumentUriUsingTree(tree, c.getString(0));
                String rel = prefix + name;

                if (MIME_DIR.equals(mime)) {
                    zip.putNextEntry(new ZipEntry(rel + "/"));
                    zip.closeEntry();
                    walk(ctx, tree, child, rel + "/", zip);
                } else {
                    zip.putNextEntry(new ZipEntry(rel));
                    try (InputStream in = ctx.getContentResolver().openInputStream(child)) {
                        if (in != null) copy(in, zip);
                    }
                    zip.closeEntry();
                }
            }
        }
    }

    static JSONObject restore(Context ctx, String treeUri, String zipPath) throws Exception {
        int files = 0;
        try (ZipInputStream zip = new ZipInputStream(new java.io.BufferedInputStream(
                new java.io.FileInputStream(zipPath), 65536))) {
            ZipEntry e;
            while ((e = zip.getNextEntry()) != null) {
                String name = e.getName();
                if (name == null) continue;
                name = name.replace('\\', '/');
                while (name.startsWith("/")) name = name.substring(1);
                if (name.isEmpty()) continue;
                if (name.contains("..")) throw new IllegalArgumentException("Unsafe backup entry");

                if (e.isDirectory()) {
                    dir(ctx, treeUri, name, true);
                } else {
                    writeStream(ctx, treeUri, name, zip);
                    files++;
                }
                zip.closeEntry();
            }
        }
        JSONObject o = new JSONObject();
        o.put("restored", files);
        return o;
    }
}
