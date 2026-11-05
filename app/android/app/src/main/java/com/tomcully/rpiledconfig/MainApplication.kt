// RPiLEDConfig - app. Copyright (C) 2026 Tom Cully <mail@tomcully.com>. Licensed under the Apache License, Version 2.0 - see LICENSE.
package com.tomcully.rpiledconfig

import android.app.Application
import com.facebook.react.PackageList
import com.facebook.react.ReactApplication
import com.facebook.react.ReactHost
import com.facebook.react.ReactNativeApplicationEntryPoint.loadReactNative
import com.facebook.react.defaults.DefaultReactHost.getDefaultReactHost
import com.tomcully.ledreader.LedReaderRegistration

class MainApplication : Application(), ReactApplication {

  override val reactHost: ReactHost by lazy {
    getDefaultReactHost(
      context = applicationContext,
      packageList =
        PackageList(this).packages.apply {
          // The app's own native modules (random bytes, the phone's SSID): not autolinked.
          add(RpiPackage())
        },
    )
  }

  override fun onCreate() {
    super.onCreate()
    // The LED reader, a VisionCamera frame-processor plugin, before any JS asks for it.
    LedReaderRegistration.register()
    loadReactNative(this)
  }
}
