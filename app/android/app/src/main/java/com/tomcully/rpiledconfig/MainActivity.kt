// RPiLEDConfig - app. Copyright (C) 2026 T. Cully. Licensed under the Apache License, Version 2.0 - see LICENSE.
package com.tomcully.rpiledconfig

import android.os.Bundle
import android.os.SystemClock
import androidx.core.splashscreen.SplashScreen.Companion.installSplashScreen
import com.facebook.react.ReactActivity
import com.facebook.react.ReactActivityDelegate
import com.facebook.react.defaults.DefaultNewArchitectureEntryPoint.fabricEnabled
import com.facebook.react.defaults.DefaultReactActivityDelegate

class MainActivity : ReactActivity() {
  override fun onCreate(savedInstanceState: Bundle?) {
    // Android's own splash (Theme.App.SplashScreen), held for a quarter of a second so it reads
    // as a splash and not a flash; the app's background is behind it and behind the first screen.
    val splash = installSplashScreen()
    val shownAt = SystemClock.uptimeMillis()
    splash.setKeepOnScreenCondition { SystemClock.uptimeMillis() - shownAt < SPLASH_MS }
    super.onCreate(null)
  }

  override fun getMainComponentName(): String = "RPiLEDConfig"

  override fun createReactActivityDelegate(): ReactActivityDelegate =
    DefaultReactActivityDelegate(this, mainComponentName, fabricEnabled)

  companion object {
    const val SPLASH_MS = 250L
  }
}
