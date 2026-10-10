package org.wwhdrecomp.wwhd;

import java.io.File;
import java.util.ArrayList;
import java.util.Arrays;
import org.json.JSONObject;

/** One isolated SDL/GPU lifetime. Never loads or starts the guest game threads. */
public final class ShaderPrepareActivity extends WwhdActivity {
    @Override public void loadLibraries() {
        super.loadLibraries();
        org.libsdl.app.SDLActivity.nativeSetenv("WWHD_NO_AUDIO", "1");
    }

    @Override protected void onCreate(android.os.Bundle state) {
        super.onCreate(state);
        if (mLayout != null) {
            android.widget.TextView note = new android.widget.TextView(this);
            note.setText("Preparing shaders for this phone. Back skips this step; shaders can compile during play.");
            note.setTextColor(android.graphics.Color.WHITE);
            note.setBackgroundColor(0xCC000000);
            note.setPadding(24, 24, 24, 24);
            mLayout.addView(note, new android.widget.RelativeLayout.LayoutParams(
                android.view.ViewGroup.LayoutParams.MATCH_PARENT,
                android.view.ViewGroup.LayoutParams.WRAP_CONTENT));
        }
    }

    @Override protected boolean retainNativeSessionOnRecreation() { return false; }

    @Override protected String[] getArguments() {
        ArrayList<String> args = new ArrayList<>(Arrays.asList(super.getArguments()));
        args.add("--warm-shaders");
        args.add("--shader-prebuild-root");
        args.add(new File(shaderUserDirectory(), "shader-prebuild").getAbsolutePath());
        args.add("--shader-prebuild-result");
        args.add(getIntent().getStringExtra("result"));
        return args.toArray(new String[0]);
    }

    @Override protected void onDestroy() {
        try {
            String path = getIntent().getStringExtra("result");
            if (path != null && !new File(path).isFile()) {
                File root = new File(shaderUserDirectory(), "shader-prebuild");
                // This identity-bound user request is an independent atomic file,
                // not publication of an active generation. Do not use Java
                // FileLock here: POSIX fcntl locks do not interoperate with flock.
                JSONObject active = SetupStore.read(new File(root, "active.json"));
                SetupStore.write(new File(root, "skip.json"), new JSONObject().put("schema", 1)
                    .put("identity", active.getJSONObject("identity")));
                new File(root, "rebuild.request").delete();
                SetupStore.write(new File(path), new JSONObject().put("schema", 1).put("status", "skipped")
                    .put("message", "Shader preparation closed; the game can compile during play"));
            }
        } catch (Exception ignored) { }
        super.onDestroy();
        // This activity is always in :shaderprep; do not reuse initialized GPU
        // globals for gameplay, and never terminate the setup/game process.
        android.os.Process.killProcess(android.os.Process.myPid());
    }
}
