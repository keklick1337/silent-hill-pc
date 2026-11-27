// SPDX-License-Identifier: GPL-3.0-or-later
package io.github.keklick1337.silenthillpc;

import android.content.ActivityNotFoundException;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.OpenableColumns;
import android.system.Os;
import android.util.Log;
import android.view.Display;
import android.view.KeyEvent;
import android.view.WindowManager;

import org.libsdl.app.SDL;
import org.libsdl.app.SDLActivity;

import java.io.BufferedInputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * The game on SDL's activity. Before the native game starts, the base data the APK carries (gamedata/, assets/,
 * config.cfg, the licenses: assets/base.zip, made by tools/android/build.sh) is unpacked into the app's own storage,
 * which becomes the game's root (SH_ANDROID_ROOT, read by src/engine/app/app_bundle.cpp). The import, config.cfg,
 * saves, logs and mods live there too.
 */
public class SilentHillActivity extends SDLActivity {
    private static final String TAG = "SilentHillPC";
    private static final String STAMP_FILE = ".apk-files";
    private static final String BASE_ASSET = "base.zip";
    private static final String STAMP_ASSET = "base.stamp";

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        File root = gameRoot(this);
        try {
            unpackBaseData(root);
        } catch (IOException e) {
            Log.e(TAG, "cannot unpack the base data into " + root, e);
        }
        try {
            Os.setenv("SH_ANDROID_ROOT", root.getAbsolutePath(), true);
        } catch (Exception e) {
            Log.e(TAG, "cannot name the game's root", e);
        }
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }

    /**
     * A debuggable build takes the game's command line from the launching intent, for testing without the file
     * picker: adb shell am start -n <package>/.SilentHillActivity --es image /sdcard/Android/data/<package>/files/sh.zip
     * (a path the app may read) or --es args "--image <path> --reimport".
     */
    @Override
    protected String[] getArguments() {
        ArrayList<String> args = new ArrayList<>();
        if ((getApplicationInfo().flags & ApplicationInfo.FLAG_DEBUGGABLE) != 0 && getIntent() != null) {
            String line = getIntent().getStringExtra("args");
            if (line != null) {
                for (String word : line.trim().split("\\s+")) {
                    if (!word.isEmpty()) {
                        args.add(word);
                    }
                }
            }
            String image = getIntent().getStringExtra("image");
            if (image != null) {
                args.add("--image");
                args.add(image);
            }
        }
        return args.toArray(new String[0]);
    }

    /** The system's back key is the game's Esc (menus, pause), as on a keyboard. */
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        if (event.getKeyCode() == KeyEvent.KEYCODE_BACK) {
            event = new KeyEvent(event.getDownTime(), event.getEventTime(), event.getAction(),
                    KeyEvent.KEYCODE_ESCAPE, event.getRepeatCount(), event.getMetaState(), event.getDeviceId(),
                    event.getScanCode(), event.getFlags(), event.getSource());
        }
        return super.dispatchKeyEvent(event);
    }

    /** The app-specific folder on the shared storage when there is one (the user can reach it), else internal. */
    static File gameRoot(Context context) {
        File external = context.getExternalFilesDir(null);
        if (external != null && (external.isDirectory() || external.mkdirs())) {
            return external;
        }
        return context.getFilesDir();
    }

    /**
     * Unpacks base.zip into the root once per build of the APK. What the root already has and what the user changed
     * stays: config.cfg only when there is none, and nothing is deleted; every other file of the APK replaces an
     * older one of the same name, as the desktop build's copy of gamedata/ does.
     */
    private void unpackBaseData(File root) throws IOException {
        String stamp = readAsset(STAMP_ASSET);
        File stampFile = new File(root, STAMP_FILE);
        if (stamp.equals(readFile(stampFile))) {
            return;
        }
        long started = System.currentTimeMillis();
        String rootPath = root.getCanonicalPath() + File.separator;
        byte[] buffer = new byte[1 << 16];
        int files = 0;
        try (ZipInputStream zip = new ZipInputStream(new BufferedInputStream(getAssets().open(BASE_ASSET)))) {
            for (ZipEntry entry; (entry = zip.getNextEntry()) != null; ) {
                File target = new File(root, entry.getName());
                if (!target.getCanonicalPath().startsWith(rootPath)) {
                    throw new IOException("bad entry " + entry.getName());
                }
                if (entry.isDirectory()) {
                    target.mkdirs();
                    continue;
                }
                if (entry.getName().equals("config.cfg") && target.exists()) {
                    continue;
                }
                File parent = target.getParentFile();
                if (parent != null) {
                    parent.mkdirs();
                }
                try (OutputStream out = new FileOutputStream(target)) {
                    for (int n; (n = zip.read(buffer)) > 0; ) {
                        out.write(buffer, 0, n);
                    }
                }
                ++files;
            }
        }
        try (OutputStream out = new FileOutputStream(stampFile)) {
            out.write(stamp.getBytes("UTF-8"));
        }
        Log.i(TAG, "unpacked " + files + " files of the APK into " + root + " in "
                + (System.currentTimeMillis() - started) + " ms");
    }

    private String readAsset(String name) throws IOException {
        try (InputStream in = getAssets().open(name)) {
            return readAll(in);
        }
    }

    private static String readFile(File file) {
        try (InputStream in = new FileInputStream(file)) {
            return readAll(in);
        } catch (IOException e) {
            return "";
        }
    }

    private static String readAll(InputStream in) throws IOException {
        StringBuilder text = new StringBuilder();
        byte[] buffer = new byte[4096];
        for (int n; (n = in.read(buffer)) > 0; ) {
            text.append(new String(buffer, 0, n, "UTF-8"));
        }
        return text.toString();
    }

    // ---- the display (src/engine/platform/display_rate.cpp calls this through JNI)

    /**
     * The refresh rates of the display's modes at its current resolution, joined by commas ("60.0,120.0"); the game
     * offers a frame cap of 120 only where the screen can show it. Empty when the display is not known.
     */
    public static String displayRates(String unused) {
        Context context = SDL.getContext();
        if (!(context instanceof android.app.Activity)) {
            return "";
        }
        Display display = ((android.app.Activity) context).getWindowManager().getDefaultDisplay();
        if (display == null) {
            return "";
        }
        Display.Mode current = display.getMode();
        StringBuilder rates = new StringBuilder();
        for (Display.Mode mode : display.getSupportedModes()) {
            if (mode.getPhysicalWidth() != current.getPhysicalWidth()
                    || mode.getPhysicalHeight() != current.getPhysicalHeight()) {
                continue;
            }
            if (rates.length() > 0) {
                rates.append(',');
            }
            rates.append(mode.getRefreshRate());
        }
        return rates.toString();
    }

    // ---- documents of the file picker (src/engine/app/first_run/picked_document.cpp calls these through JNI)

    private static final String PICKED_DIR = "picked";
    private static final String COPIED_DIR = "picked-copies";

    /**
     * A path the native importer can open for a document the Storage Access Framework gave (a content:// URI). The
     * document is opened here and stays open for the whole run; a link named like the document (the importer goes by
     * the extension) points at the open descriptor. When the system lets no path reach that descriptor, the document
     * is copied into the app's storage instead (forgetDocuments removes the copy). Null when it cannot be opened.
     */
    public static String openDocument(String uriText) {
        Context context = SDL.getContext();
        Uri uri = Uri.parse(uriText);
        return openDocument(context, uri, displayName(context, uri));
    }

    private static String openDocument(Context context, Uri uri, String name) {
        String uriText = uri.toString();
        try {
            ParcelFileDescriptor document = context.getContentResolver().openFileDescriptor(uri, "r");
            if (document == null) {
                return null;
            }
            int fd = document.detachFd();
            File dir = new File(context.getCacheDir(), PICKED_DIR);
            dir.mkdirs();
            File link = new File(dir, name);
            link.delete();
            Os.symlink("/proc/self/fd/" + fd, link.getPath());
            try (InputStream probe = new FileInputStream(link)) {
                if (probe.read() >= 0) {
                    Log.i(TAG, "picked " + uriText + " as " + link);
                    return link.getPath();
                }
            } catch (IOException e) {
                Log.i(TAG, "no path reaches the open document (" + e.getMessage() + "); copying it");
            }
            link.delete();
            File copies = new File(gameRoot(context), COPIED_DIR);
            copies.mkdirs();
            File copy = new File(copies, name);
            long started = System.currentTimeMillis();
            // The stream owns the descriptor from here on, and closes it after the copy.
            try (InputStream in = new ParcelFileDescriptor.AutoCloseInputStream(ParcelFileDescriptor.adoptFd(fd));
                 OutputStream out = new FileOutputStream(copy)) {
                byte[] buffer = new byte[1 << 20];
                for (int n; (n = in.read(buffer)) > 0; ) {
                    out.write(buffer, 0, n);
                }
            }
            Log.i(TAG, "copied " + uriText + " to " + copy + " in " + (System.currentTimeMillis() - started) + " ms");
            return copy.getPath();
        } catch (Exception e) {
            Log.e(TAG, "cannot open " + uriText, e);
            return null;
        }
    }

    /** "1" for a path openDocument made. */
    public static String isPickedDocument(String path) {
        Context context = SDL.getContext();
        File file = new File(path);
        File parent = file.getParentFile();
        if (parent == null) {
            return "0";
        }
        boolean picked = parent.equals(new File(context.getCacheDir(), PICKED_DIR))
                || parent.equals(new File(gameRoot(context), COPIED_DIR));
        return picked ? "1" : "0";
    }

    /** Removes the copies openDocument made. */
    public static String forgetDocuments(String unused) {
        File copies = new File(gameRoot(SDL.getContext()), COPIED_DIR);
        File[] files = copies.listFiles();
        if (files != null) {
            for (File file : files) {
                file.delete();
            }
        }
        copies.delete();
        return "";
    }

    /**
     * Removes the one copy openDocument made at path (in picked-copies), once the mod import is done with it; any
     * other path is left alone.
     */
    public static String forgetDocument(String path) {
        File file = new File(path);
        File copies = new File(gameRoot(SDL.getContext()), COPIED_DIR);
        if (copies.equals(file.getParentFile()) && file.isFile() && !file.delete()) {
            Log.w(TAG, "cannot remove the picked copy " + file);
        }
        return "";
    }

    // ---- the disc image of the import screen (src/engine/app/first_run/progress_window.cpp calls these through JNI)

    private static final int REQUEST_DOCUMENT = 0x5349;
    /**
     * The answer to pickDocument, read by src/import/ui/picker.h (parse_pick_answer): null while nothing is picked,
     * "~name" while the picked document is opened or copied, "-" for a cancelled pick, "!reason" when no picker
     * opened, "?name" for a document that cannot be opened, else the path openDocument made. It is static, and set
     * from onActivityResult: when the system ended the process while its picker was in front, the result comes to
     * the new process, whose import screen finds it here.
     */
    private static volatile String sPickedDocument;

    /**
     * Opens the system's document picker for any openable file; "1" when it was asked for. No MIME types are named:
     * a provider may report another type for a .zip or a .bin than the system's table has, and the picker would
     * grey such a file out. The answer comes through takeDocument.
     */
    public static String pickDocument(String unused) {
        Context context = SDL.getContext();
        if (!(context instanceof SilentHillActivity)) {
            return "0";
        }
        SilentHillActivity activity = (SilentHillActivity) context;
        sPickedDocument = null;
        activity.runOnUiThread(() -> {
            try {
                Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
                intent.addCategory(Intent.CATEGORY_OPENABLE);
                intent.setType("*/*");
                activity.startActivityForResult(intent, REQUEST_DOCUMENT);
            } catch (ActivityNotFoundException e) {
                Log.e(TAG, "no document picker", e);
                sPickedDocument = "!" + e.getMessage();
            }
        });
        return "1";
    }

    /** The answer of pickDocument ("" while there is none); a final one is given once, "~name" until it is final. */
    public static String takeDocument(String unused) {
        String answer = sPickedDocument;
        if (answer == null) {
            return "";
        }
        if (!answer.startsWith("~")) {
            sPickedDocument = null;
        }
        return answer;
    }

    private void onDocumentResult(int resultCode, Intent data) {
        Uri uri = (resultCode == RESULT_OK && data != null) ? data.getData() : null;
        if (uri == null) {
            Log.i(TAG, "the document picker gave no file (result " + resultCode + ")");
            sPickedDocument = "-";
            return;
        }
        Context context = getApplicationContext();
        String name = displayName(context, uri);
        sPickedDocument = "~" + name;
        // Opening may copy the whole image, and a cloud provider downloads it first: not on the UI thread.
        new Thread(() -> {
            String path = openDocument(context, uri, name);
            sPickedDocument = path != null ? path : "?" + name;
        }, "document-open").start();
    }

    // ---- a folder of mods (src/engine/app/mod_import_picker.cpp calls these through JNI)

    private static final int REQUEST_MOD_FOLDER = 0x5348;
    private static final String TREE_DIR = "picked-tree";
    private static final int TREE_MAX_DEPTH = 12;
    private static final int TREE_MAX_FILES = 200000;
    /** null while nothing is picked or the copy runs, "-" for a cancelled or failed pick, else the copy's path. */
    private static volatile String sPickedFolder;

    /** Opens the system's folder picker; "1" when it opened. The answer comes through takeModFolder. */
    public static String pickModFolder(String unused) {
        Context context = SDL.getContext();
        if (!(context instanceof SilentHillActivity)) {
            return "0";
        }
        SilentHillActivity activity = (SilentHillActivity) context;
        sPickedFolder = null;
        activity.runOnUiThread(() -> {
            try {
                activity.startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE), REQUEST_MOD_FOLDER);
            } catch (ActivityNotFoundException e) {
                Log.e(TAG, "no folder picker", e);
                sPickedFolder = "-";
            }
        });
        return "1";
    }

    /** The answer of pickModFolder once it is there ("" until then); it is given once. */
    public static String takeModFolder(String unused) {
        String answer = sPickedFolder;
        if (answer == null) {
            return "";
        }
        sPickedFolder = null;
        return answer;
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode == REQUEST_DOCUMENT) {
            onDocumentResult(resultCode, data);
            return;
        }
        if (requestCode != REQUEST_MOD_FOLDER) {
            super.onActivityResult(requestCode, resultCode, data);
            return;
        }
        Uri tree = (resultCode == RESULT_OK && data != null) ? data.getData() : null;
        if (tree == null) {
            sPickedFolder = "-";
            return;
        }
        Context context = getApplicationContext();
        // The game reads files by path, and a picked tree has none: its files are copied into the cache first.
        new Thread(() -> {
            try {
                File base = new File(context.getCacheDir(), TREE_DIR);
                deleteTree(base);
                String rootId = DocumentsContract.getTreeDocumentId(tree);
                String name = safeName(treeName(context, tree, rootId));
                File dest = new File(base, name);
                long started = System.currentTimeMillis();
                int[] files = {0};
                copyTree(context, tree, rootId, dest, 0, files);
                Log.i(TAG, "copied the picked folder " + tree + " (" + files[0] + " files) in "
                        + (System.currentTimeMillis() - started) + " ms");
                sPickedFolder = dest.getPath();
            } catch (Exception e) {
                Log.e(TAG, "cannot copy the picked folder " + tree, e);
                sPickedFolder = "-";
            }
        }, "mod-folder-copy").start();
    }

    private static String treeName(Context context, Uri tree, String documentId) {
        Uri doc = DocumentsContract.buildDocumentUriUsingTree(tree, documentId);
        try (Cursor c = context.getContentResolver().query(doc,
                new String[] {DocumentsContract.Document.COLUMN_DISPLAY_NAME}, null, null, null)) {
            if (c != null && c.moveToFirst() && c.getString(0) != null) {
                return c.getString(0);
            }
        }
        return "mods";
    }

    private static String safeName(String name) {
        String out = name.replace('/', '_').replace('\0', '_');
        return out.isEmpty() || out.equals(".") || out.equals("..") ? "mods" : out;
    }

    private static void copyTree(Context context, Uri tree, String documentId, File dest, int depth, int[] files)
            throws IOException {
        if (depth > TREE_MAX_DEPTH) {
            return;
        }
        if (!dest.isDirectory() && !dest.mkdirs()) {
            throw new IOException("cannot make " + dest);
        }
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, documentId);
        String[] columns = {DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                DocumentsContract.Document.COLUMN_DISPLAY_NAME, DocumentsContract.Document.COLUMN_MIME_TYPE};
        try (Cursor c = context.getContentResolver().query(children, columns, null, null, null)) {
            if (c == null) {
                return;
            }
            byte[] buffer = new byte[1 << 16];
            while (c.moveToNext()) {
                String id = c.getString(0);
                String name = c.getString(1);
                if (name == null || name.isEmpty() || name.startsWith(".") || name.contains("/")) {
                    continue;
                }
                File target = new File(dest, name);
                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(c.getString(2))) {
                    copyTree(context, tree, id, target, depth + 1, files);
                    continue;
                }
                if (++files[0] > TREE_MAX_FILES) {
                    throw new IOException("too many files");
                }
                Uri doc = DocumentsContract.buildDocumentUriUsingTree(tree, id);
                try (InputStream in = context.getContentResolver().openInputStream(doc);
                     OutputStream out = new FileOutputStream(target)) {
                    if (in == null) {
                        throw new IOException("cannot open " + name);
                    }
                    for (int n; (n = in.read(buffer)) > 0; ) {
                        out.write(buffer, 0, n);
                    }
                }
            }
        }
    }

    private static void deleteTree(File file) {
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children) {
                deleteTree(child);
            }
        }
        file.delete();
    }

    private static String displayName(Context context, Uri uri) {
        String name = null;
        try (Cursor cursor = context.getContentResolver().query(uri, new String[] {OpenableColumns.DISPLAY_NAME},
                null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                name = cursor.getString(0);
            }
        } catch (Exception e) {
            Log.w(TAG, "no name for " + uri, e);
        }
        if (name == null || name.isEmpty()) {
            name = uri.getLastPathSegment() != null ? uri.getLastPathSegment() : "disc-image.zip";
        }
        return name.replace('/', '_').replace('\0', '_');
    }
}
