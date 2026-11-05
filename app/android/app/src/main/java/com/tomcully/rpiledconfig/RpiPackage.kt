// RPiLEDConfig - app. Copyright (C) 2026 T. Cully. Licensed under the Apache License, Version 2.0 - see LICENSE.
//
// The app's own native modules, the few lines of Android that React Native itself does not
// give and no library is worth adding for: random bytes from the system's secure source, the
// name of the WiFi network the phone is on, and a few remembered settings.
package com.tomcully.rpiledconfig

import android.content.Context
import android.net.wifi.WifiManager
import android.util.Base64
import com.facebook.react.ReactPackage
import com.facebook.react.bridge.NativeModule
import com.facebook.react.bridge.Promise
import com.facebook.react.bridge.ReactApplicationContext
import com.facebook.react.bridge.ReactContextBaseJavaModule
import com.facebook.react.bridge.ReactMethod
import com.facebook.react.uimanager.ViewManager
import java.security.SecureRandom

class RpiPackage : ReactPackage {
  override fun createNativeModules(reactContext: ReactApplicationContext): List<NativeModule> =
    listOf(RandomModule(reactContext), WifiInfoModule(reactContext), SettingsModule(reactContext))

  override fun createViewManagers(reactContext: ReactApplicationContext): List<ViewManager<*, *>> = emptyList()
}

// `randomBytes(n)`: n bytes from SecureRandom, base64, synchronously: SPAKE2's scalars are
// drawn in the middle of synchronous code (src/protocol/crypto.ts).
class RandomModule(context: ReactApplicationContext) : ReactContextBaseJavaModule(context) {
  private val random = SecureRandom()

  override fun getName() = "RpiRandom"

  // Whether this build was made with -Prpiledconfig.diagnostics=true (src/util/diagnostics.ts).
  override fun getConstants(): Map<String, Any> = mapOf("diagnostics" to BuildConfig.DIAGNOSTICS)

  @ReactMethod(isBlockingSynchronousMethod = true)
  fun randomBytes(length: Int): String {
    val bytes = ByteArray(length)
    random.nextBytes(bytes)
    return Base64.encodeToString(bytes, Base64.NO_WRAP)
  }
}

// `get(key)` and `set(key, value)`: strings kept on this phone between launches
// (src/native/settings.ts). `get` is synchronous, so the first screen already has them.
class SettingsModule(context: ReactApplicationContext) : ReactContextBaseJavaModule(context) {
  private val preferences = context.getSharedPreferences("rpiledconfig", Context.MODE_PRIVATE)

  override fun getName() = "RpiSettings"

  @ReactMethod(isBlockingSynchronousMethod = true)
  fun get(key: String): String? = preferences.getString(key, null)

  @ReactMethod
  fun set(key: String, value: String) {
    preferences.edit().putString(key, value).apply()
  }
}

// `ssid()`: the SSID of the network the phone is on, or null. Android gives it only with
// location permission granted and location on (src/wifi/phoneWifi.ts asks); otherwise it is
// "<unknown ssid>", which is null here.
class WifiInfoModule(context: ReactApplicationContext) : ReactContextBaseJavaModule(context) {
  override fun getName() = "RpiWifiInfo"

  @ReactMethod
  fun ssid(promise: Promise) {
    try {
      val manager = reactApplicationContext.applicationContext.getSystemService(Context.WIFI_SERVICE) as WifiManager
      @Suppress("DEPRECATION")
      val raw = manager.connectionInfo?.ssid
      if (raw == null || raw == "<unknown ssid>" || raw == WifiManager.UNKNOWN_SSID) return promise.resolve(null)
      promise.resolve(raw.removePrefix("\"").removeSuffix("\""))
    } catch (e: Exception) {
      promise.resolve(null)
    }
  }
}
