package io.github.kalibrebox;

import android.content.Intent;
import android.util.Log;

import org.qtproject.qt.android.bindings.QtActivity;

/**
 * The single Qt activity. Launches as singleTask so the db-<appkey>:// OAuth
 * redirect lands here while the app is running. The Dropbox app key is baked
 * into the manifest scheme at CMake configure time.
 */
public class MainActivity extends QtActivity {

    private static MainActivity instance;

    public MainActivity() {
        instance = this;
    }

    public static MainActivity instance() {
        return instance;
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        forwardAuthRedirect(intent);
    }

    private void forwardAuthRedirect(Intent intent) {
        if (intent == null || intent.getData() == null) {
            return;
        }
        Log.i("KalibreBox", "OAuth redirect intent received");
        nativeOnDropboxAuthUrl(intent.getData().toString());
    }

    private static native void nativeOnDropboxAuthUrl(String url);
}
