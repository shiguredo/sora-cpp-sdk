package jp.shiguredo.hello;

import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.content.ContextCompat;

import android.Manifest;
import android.content.Context;
import android.content.pm.PackageManager;
import android.content.res.AssetManager;
import android.os.Bundle;
import android.os.Handler;
import android.util.Log;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

public class MainActivity extends AppCompatActivity {
    private static final String TAG = "MainActivity";

    static {
        System.loadLibrary("hello");
    }

    private Handler handler;
    private String weightsDirectory;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        weightsDirectory = getExternalFilesDir(null).getAbsolutePath();
        copyWeightsAssetsToDirectory(weightsDirectory);

        handler = new Handler();
        handler.postDelayed(() -> {
            if (ContextCompat.checkSelfPermission(getApplicationContext(), Manifest.permission.CAMERA) == PackageManager.PERMISSION_DENIED ||
                    ContextCompat.checkSelfPermission(getApplicationContext(), Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_DENIED) {
                requestPermissionLauncher.launch(new String[]{Manifest.permission.CAMERA, Manifest.permission.RECORD_AUDIO});
            } else {
                run(getApplicationContext(), weightsDirectory);
            }
        }, 5000);
    }

    public native void run(Context ctx, String weightsDirectory);

    private ActivityResultLauncher<String[]> requestPermissionLauncher =
            registerForActivityResult(new ActivityResultContracts.RequestMultiplePermissions(), grant -> {
                if (grant.get(Manifest.permission.CAMERA) && grant.get(Manifest.permission.RECORD_AUDIO)) {
                    run(getApplicationContext(), weightsDirectory);
                } else {
                }
            });

  // https://github.com/google/lyra/blob/a00eade050a44c5f5c7581d05d6b9cda41a9a45a/android_example/java/com/example/android/lyra/MainActivity.java#L265-L288
  // からのコピー
  private void copyWeightsAssetsToDirectory(String targetDirectory) {
    try {
      AssetManager assetManager = getAssets();
      String[] files = {"lyra_config.binarypb", "lyragan.tflite",
        "quantizer.tflite", "soundstream_encoder.tflite"};
      byte[] buffer = new byte[1024];
      int amountRead;
      for (String file : files) {
        InputStream inputStream = assetManager.open(file);
        File outputFile = new File(targetDirectory, file);

        OutputStream outputStream = new FileOutputStream(outputFile);
        Log.i(TAG, "copying asset to " + outputFile.getPath());

        while ((amountRead = inputStream.read(buffer)) != -1) {
          outputStream.write(buffer, 0, amountRead);
        }
        inputStream.close();
        outputStream.close();
      }
    } catch (Exception e) {
      Log.e(TAG, "Error copying assets", e);
    }
  }
}
