package dev.hades.reconstruction;

import android.app.NativeActivity;
import android.os.Build;
import android.os.VibrationEffect;
import android.os.VibratorManager;
import android.view.InputDevice;
import android.os.Vibrator;
import android.view.View;
import android.view.WindowInsets;
import android.view.WindowInsetsController;

/**
 * Window lifecycle and the phone vibration motor only. The native engine owns
 * rendering, UI, input and decides when original rumble events fire.
 */
public final class HadesActivity extends NativeActivity {
    /** Android GameManager mode (0 unsupported, 1 standard, 2 performance, 3 battery). */
    public int gameMode() {
        if (Build.VERSION.SDK_INT < 31) return 0;
        android.app.GameManager manager = getSystemService(android.app.GameManager.class);
        return manager == null ? 0 : manager.getGameMode();
    }

    /** Connected physical controllers (Bluetooth/USB), one name per line. */
    public String connectedControllers() {
        StringBuilder names = new StringBuilder();
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice device = InputDevice.getDevice(id);
            if (device == null || device.isVirtual()) continue;
            int sources = device.getSources();
            boolean pad = (sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                || (sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
            if (!pad) continue;
            if (names.length() > 0) names.append('\n');
            names.append(device.getName());
        }
        return names.toString();
    }

    /** Native input ownership is keyed by device id, including keyboards. */
    public boolean inputDeviceConnected(int deviceId) {
        return InputDevice.getDevice(deviceId) != null;
    }

    /** Original rumble on the controller's own motors; false if it has none. */
    public boolean controllerRumble(int deviceId, float intensity, float seconds) {
        InputDevice device = InputDevice.getDevice(deviceId);
        if (device == null) return false;
        Vibrator vibrator = null;
        if (Build.VERSION.SDK_INT >= 31) {
            VibratorManager manager = device.getVibratorManager();
            int[] ids = manager.getVibratorIds();
            if (ids.length > 0) vibrator = manager.getVibrator(ids[0]);
        } else {
            vibrator = device.getVibrator();
        }
        if (vibrator == null || !vibrator.hasVibrator()) return false;
        long ms = Math.max(1, Math.min(2000, Math.round(seconds * 1000)));
        int amplitude = Math.max(1, Math.min(255, Math.round(intensity * 255)));
        vibrator.vibrate(VibrationEffect.createOneShot(ms,
            vibrator.hasAmplitudeControl() ? amplitude : VibrationEffect.DEFAULT_AMPLITUDE));
        return true;
    }

    /** Android adaptation of the original controller rumble; called from native. */
    public void rumble(float intensity, float seconds) {
        Vibrator vibrator = (Vibrator) getSystemService(VIBRATOR_SERVICE);
        if (vibrator == null || !vibrator.hasVibrator()) return;
        long ms = Math.max(1, Math.min(2000, Math.round(seconds * 1000)));
        int amplitude = Math.max(1, Math.min(255, Math.round(intensity * 255)));
        vibrator.vibrate(VibrationEffect.createOneShot(ms,
            vibrator.hasAmplitudeControl() ? amplitude : VibrationEffect.DEFAULT_AMPLITUDE));
    }

    @Override public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if (!focused) return;
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController controller = getWindow().getInsetsController();
            if (controller != null) {
                controller.setSystemBarsBehavior(
                    WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
                controller.hide(WindowInsets.Type.systemBars());
            }
        } else {
            getWindow().getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
        }
    }
}
