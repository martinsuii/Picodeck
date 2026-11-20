package com.arcademultitool.companion;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.KeyEvent;
import android.view.View;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.Toast;

public class MainActivity extends Activity {
    private static final int REQ_FILE_CHOOSER = 9001;

    private WebView web;
    private NativeBridge bridge;
    private ValueCallback<Uri[]> fileCallback;

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);

        WebView.setWebContentsDebuggingEnabled(true);

        web = new WebView(this);
        WebSettings s = web.getSettings();
        s.setJavaScriptEnabled(true);
        s.setDomStorageEnabled(true);
        s.setAllowFileAccess(true);
        s.setAllowContentAccess(true);
        s.setMediaPlaybackRequiresUserGesture(false);
        s.setCacheMode(WebSettings.LOAD_DEFAULT);
        s.setTextZoom(100);

        web.setBackgroundColor(0xFF101014);
        web.setWebViewClient(new WebViewClient());
        web.setWebChromeClient(
                new WebChromeClient() {
                    @Override
                    public boolean onShowFileChooser(
                            WebView view,
                            ValueCallback<Uri[]> callback,
                            FileChooserParams params) {
                        if (fileCallback != null) fileCallback.onReceiveValue(null);
                        fileCallback = callback;
                        try {
                            startActivityForResult(params.createIntent(), REQ_FILE_CHOOSER);
                            return true;
                        } catch (Exception e) {
                            fileCallback = null;
                            Toast.makeText(MainActivity.this, e.getMessage(), Toast.LENGTH_LONG).show();
                            return false;
                        }
                    }
                }
        );

        bridge = new NativeBridge(this, web);
        web.addJavascriptInterface(bridge, "PicoAndroid");

        web.loadUrl("file:///android_asset/web/index.html");
        setContentView(web);
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        if (request == REQ_FILE_CHOOSER) {
            if (fileCallback != null) {
                fileCallback.onReceiveValue(WebChromeClient.FileChooserParams.parseResult(result, data));
                fileCallback = null;
            }
            return;
        }
        if (bridge != null && bridge.onActivityResult(request, result, data)) return;
        super.onActivityResult(request, result, data);
    }

    @Override
    public boolean onKeyDown(int key, KeyEvent event) {
        if (key == KeyEvent.KEYCODE_BACK) {
            if (web != null && web.canGoBack()) {
                web.goBack();
                return true;
            }
            finish();
            return true;
        }
        return super.onKeyDown(key, event);
    }

    @Override
    protected void onDestroy() {
        if (bridge != null) bridge.shutdown();
        if (web != null) {
            web.removeJavascriptInterface("PicoAndroid");
            web.destroy();
        }
        super.onDestroy();
    }
}
